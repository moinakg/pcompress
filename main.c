/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 * 
 * This program includes partly-modified public domain/LGPL source
 * code from the LZMA SDK: http://www.7-zip.org/sdk.html
 */

/*
 * pcompress - Do a chunked parallel compression/decompression of a file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#if defined(sun) || defined(__sun)
#include <sys/byteorder.h>
#else
#include <byteswap.h>
#endif
#include <libgen.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>
#include <rabin_polynomial.h>

/* Needed for CLzmaEncprops. */
#include <LzmaEnc.h>

/*
 * We use 5MB chunks by default.
 */
#define	DEFAULT_CHUNKSIZE	(5 * 1024 * 1024)

struct wdata {
	struct cmp_data **dary;
	int wfd;
	int nprocs;
	ssize_t chunksize;
};


static void * writer_thread(void *dat);
static int init_algo(const char *algo, int bail);

static compress_func_ptr _compress_func;
static compress_func_ptr _decompress_func;
static init_func_ptr _init_func;
static deinit_func_ptr _deinit_func;
static stats_func_ptr _stats_func;

static int main_cancel;
static int adapt_mode = 0;
static int pipe_mode = 0;
static int nthreads = 0;
static int hide_mem_stats = 1;
static int hide_cmp_stats = 1;
static int enable_rabin_scan = 0;
static int enable_delta_encode = 0;
static int enable_rabin_split = 1;
static unsigned int chunk_num;
static uint64_t largest_chunk, smallest_chunk, avg_chunk;
static const char *exec_name;
static const char *algo = NULL;
static int do_compress = 0;
static int do_uncompress = 0;
static rabin_context_t *rctx;

static void
usage(void)
{
	fprintf(stderr,
	    "Usage:\n"
	    "1) To compress a file:\n"
	    "   %s -c <algorithm> [-l <compress level>] [-s <chunk size>] <file>\n"
	    "   Where <algorithm> can be the folowing:\n"
	    "   lzfx   - Very fast and small algorithm based on LZF.\n"
	    "   lz4    - Ultra fast, high-throughput algorithm reaching RAM B/W at level1.\n"
	    "   zlib   - The base Zlib format compression (not Gzip).\n"
	    "   lzma   - The LZMA (Lempel-Ziv Markov) algorithm from 7Zip.\n"
	    "   bzip2  - Bzip2 Algorithm from libbzip2.\n"
	    "   ppmd   - The PPMd algorithm excellent for textual data. PPMd requires\n"
	    "            at least 64MB X CPUs more memory than the other modes.\n"
	    "   adapt  - Adaptive mode where ppmd or bzip2 will be used per chunk,\n"
	    "            depending on which one produces better compression. This mode\n"
	    "            is obviously fairly slow and requires lots of memory.\n"
	    "   adapt2 - Adaptive mode which includes ppmd and lzma. This requires\n"
	    "            more memory than adapt mode, is slower and potentially gives\n"
	    "            the best compression.\n"
	    "   <chunk_size> - This can be in bytes or can use the following suffixes:\n"
	    "            g - Gigabyte, m - Megabyte, k - Kilobyte.\n"
	    "            Larger chunks produce better compression at the cost of memory.\n"
	    "   <compress_level> - Can be a number from 0 meaning minimum and 14 meaning\n"
	    "            maximum compression.\n\n"
	    "2) To decompress a file compressed using above command:\n"
	    "   %s -d <compressed file> <target file>\n"
	    "3) To operate as a pipe, read from stdin and write to stdout:\n"
	    "   %s -p ...\n"
	    "4) Attempt Rabin fingerprinting based deduplication on chunks:\n"
	    "   %s -D ...\n"
	    "   %s -D -r ... - Do NOT split chunks at a rabin boundary. Default is to split.\n"
	    "5) Perform Delta Encoding in addition to Exact Dedup:\n"
	    "   %s -E ... - This also implies '-D'.\n"
	    "6) Number of threads can optionally be specified: -t <1 - 256 count>\n"
	    "7) Pass '-M' to display memory allocator statistics\n"
	    "8) Pass '-C' to display compression statistics\n\n",
	    exec_name, exec_name, exec_name, exec_name, exec_name, exec_name);
}

void
show_compression_stats(uint64_t chunksize)
{
	fprintf(stderr, "\nCompression Statistics\n");
	fprintf(stderr, "======================\n");
	fprintf(stderr, "Total chunks           : %u\n", chunk_num);
	fprintf(stderr, "Best compressed chunk  : %s(%.2f%%)\n",
	    bytes_to_size(smallest_chunk), (double)smallest_chunk/(double)chunksize*100);
	fprintf(stderr, "Worst compressed chunk : %s(%.2f%%)\n",
	    bytes_to_size(largest_chunk), (double)largest_chunk/(double)chunksize*100);
	avg_chunk /= chunk_num;
	fprintf(stderr, "Avg compressed chunk   : %s(%.2f%%)\n\n",
	    bytes_to_size(avg_chunk), (double)avg_chunk/(double)chunksize*100);
}

/*
 * This routine is called in multiple threads. Calls the decompression handler
 * as encoded in the file header. For adaptive mode the handler adapt_decompress()
 * in turns looks at the chunk header and call the actualy decompression
 * routine.
 */
static void *
perform_decompress(void *dat)
{
	struct cmp_data *tdat = (struct cmp_data *)dat;
	ssize_t _chunksize;
	ssize_t rabin_index_sz, rabin_data_sz, rabin_index_sz_cmp, rabin_data_sz_cmp;
	int type, rv;
	unsigned int blknum;
	typeof (tdat->crc64) crc64;
	uchar_t HDR;
	uchar_t *cseg;

redo:
	sem_wait(&tdat->start_sem);
	if (unlikely(tdat->cancel)) {
		tdat->len_cmp = 0;
		sem_post(&tdat->cmp_done_sem);
		return (0);
	}

	/*
	 * If the last read returned a 0 quit.
	 */
	if (tdat->rbytes == 0) {
		tdat->len_cmp = 0;
		tdat->crc64 = 0;
		goto cont;
	}

	cseg = tdat->compressed_chunk + sizeof (crc64);
	_chunksize = tdat->chunksize;
	tdat->crc64 = htonll(*((typeof (crc64) *)(tdat->compressed_chunk)));
	HDR = *cseg;
	cseg += CHDR_SZ;
	if (HDR & CHSIZE_MASK) {
		uchar_t *rseg;

		tdat->rbytes -= sizeof (ssize_t);
		tdat->len_cmp -= sizeof (ssize_t);
		rseg = tdat->compressed_chunk + tdat->rbytes;
		_chunksize = ntohll(*((ssize_t *)rseg));
	}

	if (HDR & COMPRESSED) {
		if (enable_rabin_scan && (HDR & CHUNK_FLAG_DEDUP)) {
			uchar_t *cmpbuf, *ubuf;

			/* Extract various sizes from rabin header. */
			rabin_parse_hdr(cseg, &blknum, &rabin_index_sz, &rabin_data_sz,
					&rabin_index_sz_cmp, &rabin_data_sz_cmp, &_chunksize);
			memcpy(tdat->uncompressed_chunk, cseg, RABIN_HDR_SIZE);

			/*
			 * Uncompress the data chunk first and then uncompress the index.
			 * The uncompress routines can use extra bytes at the end for temporary
			 * state/dictionary info. Since data chunk directly follows index
			 * uncompressing index first corrupts the data.
			 */
			cmpbuf = cseg + RABIN_HDR_SIZE + rabin_index_sz_cmp;
			ubuf = tdat->uncompressed_chunk + RABIN_HDR_SIZE + rabin_index_sz;
			rv = tdat->decompress(cmpbuf, rabin_data_sz_cmp, ubuf, &_chunksize,
			    tdat->level, HDR, tdat->data);
			if (rv == -1) {
				tdat->len_cmp = 0;
				fprintf(stderr, "ERROR: Chunk %d, decompression failed.\n", tdat->id);
				goto cont;
			}

			rv = 0;
			cmpbuf = cseg + RABIN_HDR_SIZE;
			ubuf = tdat->uncompressed_chunk + RABIN_HDR_SIZE;
			if (rabin_index_sz >= 90) {
				/* Index should be at least 90 bytes to have been compressed. */
				rv = lzma_decompress(cmpbuf, rabin_index_sz_cmp, ubuf,
				    &rabin_index_sz, tdat->rctx->level, 0, tdat->rctx->lzma_data);
			} else {
				memcpy(ubuf, cmpbuf, rabin_index_sz);
			}
		} else {
			rv = tdat->decompress(cseg, tdat->len_cmp, tdat->uncompressed_chunk,
			    &_chunksize, tdat->level, HDR, tdat->data);
		}
	} else {
		memcpy(cseg + CHDR_SZ, tdat->uncompressed_chunk, _chunksize);
	}
	tdat->len_cmp = _chunksize;

	if (rv == -1) {
		tdat->len_cmp = 0;
		fprintf(stderr, "ERROR: Chunk %d, decompression failed.\n", tdat->id);
		goto cont;
	}
	/* Rebuild chunk from dedup blocks. */
	if (enable_rabin_scan && (HDR & CHUNK_FLAG_DEDUP)) {
		rabin_context_t *rctx;
		uchar_t *tmp;

		rctx = tdat->rctx;
		reset_rabin_context(tdat->rctx);
		rctx->cbuf = tdat->compressed_chunk;
		rabin_inverse_dedup(rctx, tdat->uncompressed_chunk, &(tdat->len_cmp));
		if (!rctx->valid) {
			fprintf(stderr, "ERROR: Chunk %d, dedup recovery failed.\n", tdat->id);
			rv = -1;
			tdat->len_cmp = 0;
			goto cont;
		}
		_chunksize = tdat->len_cmp;
		tmp = tdat->uncompressed_chunk;
		tdat->uncompressed_chunk = tdat->compressed_chunk;
		tdat->compressed_chunk = tmp;
		tdat->cmp_seg = tdat->uncompressed_chunk;
	}

	/*
	 * Re-compute checksum of original uncompressed chunk.
	 * If it does not match we set length of chunk to 0 to indicate
	 * exit to the writer thread.
	 */
	crc64 = lzma_crc64(tdat->uncompressed_chunk, _chunksize, 0);
	if (crc64 != tdat->crc64) {
		tdat->len_cmp = 0;
		fprintf(stderr, "ERROR: Chunk %d, checksums do not match.\n", tdat->id);
	}

cont:
	sem_post(&tdat->cmp_done_sem);
	goto redo;
}

/*
 * File decompression routine.
 *
 * Compressed file Format
 * ----------------------
 * File Header:
 * Algorithm string:  8 bytes.
 * Version number:    2 bytes.
 * Global Flags:      2 bytes.
 * Chunk size:        8 bytes.
 * Compression Level: 4 bytes.
 *
 * Chunk Header:
 * Compressed length: 8 bytes.
 * CRC64 Checksum:    8 bytes.
 * Chunk flags:       1 byte.
 * 
 * Chunk Flags, 8 bits:
 * I  I  I  I  I  I  I  I
 * |  |     |        |  |
 * |  '-----'        |  `- 0 - Uncompressed
 * |     |           |     1 - Compressed
 * |     |           |   
 * |     |           `---- 1 - Chunk was Deduped
 * |     |
 * |     |                 1 - Bzip2 (Adaptive Mode)
 * |     `---------------- 2 - Lzma (Adaptive Mode)
 * |                       3 - PPMD (Adaptive Mode)
 * |
 * `---------------------- 1 - Last Chunk flag
 *
 * A file trailer to indicate end.
 * Zero Compressed length: 8 zero bytes.
 */
#define UNCOMP_BAIL err = 1; goto uncomp_done

static void
start_decompress(const char *filename, const char *to_filename)
{
	char tmpfile[MAXPATHLEN];
	char algorithm[ALGO_SZ];
	struct stat sbuf;
	struct wdata w;
	int compfd = -1, i, p;
	int uncompfd = -1, err, np, bail;
	int nprocs, thread = 0, level;
	short version, flags;
	ssize_t chunksize, compressed_chunksize;
	struct cmp_data **dary, *tdat;
	pthread_t writer_thr;

	err = 0;
	flags = 0;
	thread = 0;

	/*
	 * Open files and do sanity checks.
	 */
	if (!pipe_mode) {
		if ((compfd = open(filename, O_RDONLY, 0)) == -1)
			err_exit(1, "Cannot open: %s", filename);

		if (fstat(compfd, &sbuf) == -1)
			err_exit(1, "Cannot stat: %s", filename);
		if (sbuf.st_size == 0)
			return;

		if ((uncompfd = open(to_filename, O_WRONLY|O_CREAT|O_TRUNC, 0)) == -1) {
			close(compfd);
			err_exit(1, "Cannot open: %s", to_filename);
		}
	} else {
		compfd = fileno(stdin);
		if (compfd == -1) {
			perror("fileno ");
			UNCOMP_BAIL;
		}
		uncompfd = fileno(stdout);
		if (uncompfd == -1) {
			perror("fileno ");
			UNCOMP_BAIL;
		}
	}

	/*
	 * Read file header pieces and verify.
	 */
	if (Read(compfd, algorithm, ALGO_SZ) < ALGO_SZ) {
		perror("Read: ");
		UNCOMP_BAIL;
	}
	if (init_algo(algorithm, 0) != 0) {
		fprintf(stderr, "%s is not a pcompressed file.\n", filename);
		UNCOMP_BAIL;
	}
	algo = algorithm;

	if (Read(compfd, &version, sizeof (version)) < sizeof (version) ||
	    Read(compfd, &flags, sizeof (flags)) < sizeof (flags) ||
	    Read(compfd, &chunksize, sizeof (chunksize)) < sizeof (chunksize) ||
	    Read(compfd, &level, sizeof (level)) < sizeof (level)) {
		perror("Read: ");
		UNCOMP_BAIL;
	}

	version = ntohs(version);
	flags = ntohs(flags);
	chunksize = ntohll(chunksize);
	level = ntohl(level);

	if (version != VERSION) {
		fprintf(stderr, "Unsupported version: %d\n", version);
		err = 1;
		goto uncomp_done;
	}

	compressed_chunksize = chunksize + (chunksize >> 6) + sizeof (uint64_t)
	    + sizeof (chunksize);

	if (flags & FLAG_DEDUP) {
		enable_rabin_scan = 1;
	}

	nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	if (nthreads > 0 && nthreads < nprocs)
		nprocs = nthreads;

	fprintf(stderr, "Scaling to %d threads\n", nprocs);
	slab_cache_add(compressed_chunksize + CHDR_SZ);
	slab_cache_add(chunksize);
	slab_cache_add(sizeof (struct cmp_data));

	dary = (struct cmp_data **)slab_alloc(NULL, sizeof (struct cmp_data *) * nprocs);
	for (i = 0; i < nprocs; i++) {
		dary[i] = (struct cmp_data *)slab_alloc(NULL, sizeof (struct cmp_data));
		if (!dary[i]) {
			fprintf(stderr, "Out of memory\n");
			UNCOMP_BAIL;
		}
		tdat = dary[i];
		tdat->compressed_chunk = (uchar_t *)slab_alloc(NULL,
		    compressed_chunksize + CHDR_SZ);
		if (!tdat->compressed_chunk) {
			fprintf(stderr, "Out of memory\n");
			UNCOMP_BAIL;
		}
		if (enable_rabin_scan)
			tdat->uncompressed_chunk = (uchar_t *)slab_alloc(NULL,
			    compressed_chunksize + CHDR_SZ);
		else
			tdat->uncompressed_chunk = (uchar_t *)slab_alloc(NULL, chunksize);
		if (!tdat->uncompressed_chunk) {
			fprintf(stderr, "Out of memory\n");
			UNCOMP_BAIL;
		}
		tdat->cmp_seg = tdat->uncompressed_chunk;
		tdat->chunksize = chunksize;
		tdat->compress = _compress_func;
		tdat->decompress = _decompress_func;
		tdat->cancel = 0;
		tdat->level = level;
		sem_init(&(tdat->start_sem), 0, 0);
		sem_init(&(tdat->cmp_done_sem), 0, 0);
		sem_init(&(tdat->write_done_sem), 0, 1);
		if (_init_func) {
			if (_init_func(&(tdat->data), &(tdat->level), chunksize) != 0) {
				UNCOMP_BAIL;
			}
		}
		if (enable_rabin_scan)
			tdat->rctx = create_rabin_context(chunksize, compressed_chunksize,
			    algo, enable_delta_encode);
		else
			tdat->rctx = NULL;
		if (pthread_create(&(tdat->thr), NULL, perform_decompress,
		    (void *)tdat) != 0) {
			perror("Error in thread creation: ");
			UNCOMP_BAIL;
		}
	}
	thread = 1;

	w.dary = dary;
	w.wfd = uncompfd;
	w.nprocs = nprocs;
	w.chunksize = chunksize;
	if (pthread_create(&writer_thr, NULL, writer_thread, (void *)(&w)) != 0) {
		perror("Error in thread creation: ");
		UNCOMP_BAIL;
	}

	/*
	 * Now read from the compressed file in variable compressed chunk size.
	 * First the size is read from the chunk header and then as many bytes +
	 * CRC64 checksum size are read and passed to decompression thread.
	 * Chunk sequencing is ensured.
	 */
	chunk_num = 0;
	np = 0;
	bail = 0;
	while (!bail) {
		ssize_t rb;

		if (main_cancel) break;
		for (p = 0; p < nprocs; p++) {
			np = p;
			tdat = dary[p];
			sem_wait(&tdat->write_done_sem);
			if (main_cancel) break;
			tdat->id = chunk_num;

			/*
			 * First read length of compressed chunk.
			 */
			rb = Read(compfd, &tdat->len_cmp, sizeof (tdat->len_cmp));
			if (rb != sizeof (tdat->len_cmp)) {
				if (rb < 0) perror("Read: ");
				else
					fprintf(stderr, "Incomplete chunk %d header,"
					    "file corrupt\n", chunk_num);
				UNCOMP_BAIL;
			}
			tdat->len_cmp = htonll(tdat->len_cmp);

			/*
			 * Zero compressed len means end of file.
			 */
			if (tdat->len_cmp == 0) {
				bail = 1;
				break;
			}

			if (tdat->len_cmp > largest_chunk)
				largest_chunk = tdat->len_cmp;
			if (tdat->len_cmp < smallest_chunk)
				smallest_chunk = tdat->len_cmp;
			avg_chunk += tdat->len_cmp;

			/*
			 * Now read compressed chunk including the crc64 checksum.
			 */
			tdat->rbytes = Read(compfd, tdat->compressed_chunk,
			    tdat->len_cmp + sizeof(tdat->crc64) + CHDR_SZ);
			if (main_cancel) break;
			if (tdat->rbytes < tdat->len_cmp + sizeof(tdat->crc64) + CHDR_SZ) {
				if (tdat->rbytes < 0) {
					perror("Read: ");
					UNCOMP_BAIL;
				} else {
					fprintf(stderr, "Incomplete chunk %d, file corrupt.\n",
					    chunk_num);
					UNCOMP_BAIL;
				}
			}
			sem_post(&tdat->start_sem);
			chunk_num++;
		}
	}


	if (!main_cancel) {
		for (p = 0; p < nprocs; p++) {
			if (p == np) continue;
			tdat = dary[p];
			sem_wait(&tdat->write_done_sem);
		}
		thread = 0;
	}
uncomp_done:
	if (thread) {
		for (i = 0; i < nprocs; i++) {
			tdat = dary[i];
			tdat->cancel = 1;
			tdat->len_cmp = 0;
			sem_post(&tdat->start_sem);
			sem_post(&tdat->cmp_done_sem);
			pthread_join(tdat->thr, NULL);
		}
		pthread_join(writer_thr, NULL);
	}

	/*
	 * Ownership and mode of target should be same as original.
	 */
	fchmod(uncompfd, sbuf.st_mode);
	if (fchown(uncompfd, sbuf.st_uid, sbuf.st_gid) == -1)
		perror("Chown ");
	if (dary != NULL) {
		for (i = 0; i < nprocs; i++) {
			slab_free(NULL, dary[i]->uncompressed_chunk);
			slab_free(NULL, dary[i]->compressed_chunk);
			if (_deinit_func)
				_deinit_func(&(tdat->data));
			if (enable_rabin_scan) {
				destroy_rabin_context(dary[i]->rctx);
			}
			slab_free(NULL, dary[i]);
		}
		slab_free(NULL, dary);
	}
	if (!pipe_mode) {
		if (compfd != -1) close(compfd);
		if (uncompfd != -1) close(uncompfd);
	}

	if (!hide_cmp_stats) show_compression_stats(chunksize);
	slab_cleanup(hide_mem_stats);
}

static void *
perform_compress(void *dat) {
	struct cmp_data *tdat = (struct cmp_data *)dat;
	typeof (tdat->chunksize) _chunksize, len_cmp, rabin_index_sz, index_size_cmp;
	int type, rv;
	uchar_t *compressed_chunk;

redo:
	sem_wait(&tdat->start_sem);
	if (unlikely(tdat->cancel)) {
		tdat->len_cmp = 0;
		sem_post(&tdat->cmp_done_sem);
		return (0);
	}

	compressed_chunk = tdat->compressed_chunk + CHDR_SZ;
	/* Perform Dedup if enabled. */
	if (enable_rabin_scan) {
		rabin_context_t *rctx;
		ssize_t rbytes;

		/*
		 * Compute checksum of original uncompressed chunk.
		 */
		tdat->crc64 = lzma_crc64(tdat->cmp_seg, tdat->rbytes, 0);

		rctx = tdat->rctx;
		rbytes = tdat->rbytes;
		reset_rabin_context(tdat->rctx);
		rctx->cbuf = tdat->uncompressed_chunk;
		rabin_index_sz = rabin_dedup(tdat->rctx, tdat->cmp_seg, &(tdat->rbytes), 0, NULL);
		if (!rctx->valid) {
			memcpy(tdat->uncompressed_chunk, tdat->cmp_seg, rbytes);
			tdat->rbytes = rbytes;
		}
	} else {
		/*
		 * Compute checksum of original uncompressed chunk.
		 */
		tdat->crc64 = lzma_crc64(tdat->uncompressed_chunk, tdat->rbytes, 0);
	}

	/*
	 * If doing dedup we compress rabin index and deduped data separately.
	 * The rabin index array values can pollute the compressor's dictionary thereby
	 * reducing compression effectiveness of the data chunk. So we separate them.
	 */
	if (enable_rabin_scan && tdat->rctx->valid) {
		_chunksize = tdat->rbytes - rabin_index_sz - RABIN_HDR_SIZE;
		index_size_cmp = rabin_index_sz;
		memcpy(compressed_chunk, tdat->uncompressed_chunk, RABIN_HDR_SIZE);

		rv = 0;
		if (rabin_index_sz >= 90) {
			/* Compress index if it is at least 90 bytes. */
			rv = lzma_compress(tdat->uncompressed_chunk + RABIN_HDR_SIZE,
			    rabin_index_sz, compressed_chunk + RABIN_HDR_SIZE,
			    &index_size_cmp, tdat->rctx->level, 0, tdat->rctx->lzma_data);
		} else {
			memcpy(compressed_chunk + RABIN_HDR_SIZE,
			    tdat->uncompressed_chunk + RABIN_HDR_SIZE, rabin_index_sz);
		}

		index_size_cmp += RABIN_HDR_SIZE;
		rabin_index_sz += RABIN_HDR_SIZE;
		if (rv == 0) {
			/* Compress data chunk. */
			rv = tdat->compress(tdat->uncompressed_chunk + rabin_index_sz,
			    _chunksize, compressed_chunk + index_size_cmp, &_chunksize,
		            tdat->level, 0, tdat->data);
			/* Now update rabin header with the compressed sizes. */
			rabin_update_hdr(compressed_chunk, index_size_cmp - RABIN_HDR_SIZE,
					 _chunksize);
		}
		_chunksize += index_size_cmp;
	} else {
		_chunksize = tdat->rbytes;
		rv = tdat->compress(tdat->uncompressed_chunk, tdat->rbytes,
		    compressed_chunk, &_chunksize, tdat->level, 0, tdat->data);
	}
	/*
	 * Sanity check to ensure compressed data is lesser than original.
	 * If at all compression expands/does not shrink data then the chunk
	 * will be left uncompressed. Also if the compression errored the
	 * chunk will be left uncompressed.
	 */
	tdat->len_cmp = _chunksize;
	if (_chunksize >= tdat->chunksize || rv < 0) {
		memcpy(compressed_chunk, tdat->uncompressed_chunk, tdat->rbytes);
		type = UNCOMPRESSED;
		tdat->len_cmp = tdat->rbytes;
	} else {
		type = COMPRESSED;
	}

	if (enable_rabin_scan && tdat->rctx->valid) {
		type |= CHUNK_FLAG_DEDUP;
	}
	/*
	 * Insert compressed chunk length and CRC64 checksum into
	 * chunk header.
	 */
	len_cmp = tdat->len_cmp;
	*((typeof (len_cmp) *)(tdat->cmp_seg)) = htonll(tdat->len_cmp);
	*((typeof (tdat->crc64) *)(tdat->cmp_seg + sizeof (tdat->len_cmp))) = htonll(tdat->crc64);
	tdat->len_cmp += CHDR_SZ;
	tdat->len_cmp += sizeof (len_cmp);
	tdat->len_cmp += sizeof (tdat->crc64);

	if (adapt_mode)
		type |= (rv << 4);

	/*
	 * If chunk is less than max chunksize, store this length as well.
	 */
	if (tdat->rbytes < tdat->chunksize) {
		type |= CHSIZE_MASK;
		*((typeof (tdat->rbytes) *)(tdat->cmp_seg + tdat->len_cmp)) = htonll(tdat->rbytes);
		tdat->len_cmp += sizeof (tdat->rbytes);
		len_cmp += sizeof (tdat->rbytes);
		*((typeof (len_cmp) *)(tdat->cmp_seg)) = htonll(len_cmp);
	}
	/*
	 * Set the chunk header flags.
	 */
	*(tdat->compressed_chunk) = type;

cont:
	sem_post(&tdat->cmp_done_sem);
	goto redo;
}

static void *
writer_thread(void *dat) {
	int p;
	struct wdata *w = (struct wdata *)dat;
	struct cmp_data *tdat;
	ssize_t wbytes;

repeat:
	for (p = 0; p < w->nprocs; p++) {
		tdat = w->dary[p];
		sem_wait(&tdat->cmp_done_sem);
		if (tdat->len_cmp == 0) {
			goto do_cancel;
		}

		if (do_compress) {
			if (tdat->len_cmp > largest_chunk)
				largest_chunk = tdat->len_cmp;
			if (tdat->len_cmp < smallest_chunk)
				smallest_chunk = tdat->len_cmp;
			avg_chunk += tdat->len_cmp;
		}
		wbytes = Write(w->wfd, tdat->cmp_seg, tdat->len_cmp);
		if (unlikely(wbytes != tdat->len_cmp)) {
			int i;

			perror("Chunk Write: ");
do_cancel:
			main_cancel = 1;
			for (i = 0; i < w->nprocs; i++) {
				tdat->cancel = 1;
				sem_post(&tdat->start_sem);
				sem_post(&tdat->write_done_sem);
			}
			return (0);
		}
		sem_post(&tdat->write_done_sem);
	}
	goto repeat;
}

/*
 * File compression routine. Can use as many threads as there are
 * logical cores unless user specified something different. There is
 * not much to gain from nthreads > n logical cores however.
 */
#define COMP_BAIL err = 1; goto comp_done

void
start_compress(const char *filename, uint64_t chunksize, int level)
{
	struct wdata w;
	char tmpfile1[MAXPATHLEN];
	char to_filename[MAXPATHLEN];
	ssize_t compressed_chunksize;
	ssize_t n_chunksize, rbytes, rabin_count;
	short version, flags;
	struct stat sbuf;
	int compfd = -1, uncompfd = -1, err;
	int i, thread = 0, bail;
	int nprocs, np, p;
	struct cmp_data **dary = NULL, *tdat;
	pthread_t writer_thr;
	uchar_t *cread_buf, *pos;
	rabin_context_t *rctx;

	/*
	 * Compressed buffer size must include zlib scratch space and
	 * chunk header space.
	 * See http://www.zlib.net/manual.html#compress2
	 * 
	 * We do this unconditionally whether user mentioned zlib or not
	 * to keep it simple. While zlib scratch space is only needed at
	 * runtime, chunk header is stored in the file.
	 *
	 * See start_decompress() routine for details of chunk header.
	 * We also keep extra 8-byte space for the last chunk's size.
	 */
	compressed_chunksize = chunksize + (chunksize >> 6) +
	    sizeof (chunksize) + sizeof (uint64_t) + sizeof (chunksize);
	err = 0;
	flags = 0;
	thread = 0;
	slab_cache_add(chunksize);
	slab_cache_add(compressed_chunksize + CHDR_SZ);
	slab_cache_add(sizeof (struct cmp_data));

	if (enable_rabin_scan) {
		flags |= FLAG_DEDUP;
	}

	/* A host of sanity checks. */
	if (!pipe_mode) {
		if ((uncompfd = open(filename, O_RDWR, 0)) == -1)
			err_exit(1, "Cannot open: %s", filename);

		if (fstat(uncompfd, &sbuf) == -1) {
			close(uncompfd);
			err_exit(1, "Cannot stat: %s", filename);
		}

		if (!S_ISREG(sbuf.st_mode)) {
			close(uncompfd);
			err_exit(0, "File %s is not a regular file.\n", filename);
		}

		if (sbuf.st_size == 0) {
			close(uncompfd);
			return;
		}

		/*
		 * Adjust chunk size for small files. We then get an archive with
		 * a single chunk for the entire file.
		 */
		if (sbuf.st_size < chunksize) {
			chunksize = sbuf.st_size;
			nthreads = 1;
		} else {
			if (nthreads == 0 || nthreads > sbuf.st_size / chunksize) {
				nthreads = sbuf.st_size / chunksize;
				if (sbuf.st_size % chunksize)
					nthreads++;
			}
		}

		/*
		 * Create a temporary file to hold compressed data which is renamed at
		 * the end. The target file name is same as original file with the '.pz'
		 * extension appended.
		 */
		strcpy(tmpfile1, filename);
		strcpy(tmpfile1, dirname(tmpfile1));
		strcat(tmpfile1, "/.pcompXXXXXX");
		snprintf(to_filename, sizeof (to_filename), "%s" COMP_EXTN, filename);
		if ((compfd = mkstemp(tmpfile1)) == -1) {
			perror("mkstemp ");
			COMP_BAIL;
		}
	} else {
		/*
		 * Use stdin/stdout for pipe mode.
		 */
		compfd = fileno(stdout);
		if (compfd == -1) {
			perror("fileno ");
			COMP_BAIL;
		}
		uncompfd = fileno(stdin);
		if (uncompfd == -1) {
			perror("fileno ");
			COMP_BAIL;
		}
	}

	nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	if (nthreads > 0 && nthreads < nprocs)
		nprocs = nthreads;

	fprintf(stderr, "Scaling to %d thread", nprocs);
	if (nprocs > 1) fprintf(stderr, "s");
	fprintf(stderr, "\n");

	dary = (struct cmp_data **)slab_alloc(NULL, sizeof (struct cmp_data *) * nprocs);
	if (enable_rabin_scan)
		cread_buf = (uchar_t *)slab_alloc(NULL, compressed_chunksize + CHDR_SZ);
	else
		cread_buf = (uchar_t *)slab_alloc(NULL, chunksize);
	if (!cread_buf) {
		fprintf(stderr, "Out of memory\n");
		COMP_BAIL;
	}

	for (i = 0; i < nprocs; i++) {
		dary[i] = (struct cmp_data *)slab_alloc(NULL, sizeof (struct cmp_data));
		if (!dary[i]) {
			fprintf(stderr, "Out of memory\n");
			COMP_BAIL;
		}
		tdat = dary[i];
		tdat->cmp_seg = (uchar_t *)slab_alloc(NULL, compressed_chunksize + CHDR_SZ);
		tdat->compressed_chunk = tdat->cmp_seg + sizeof (chunksize) + sizeof (uint64_t);
		if (!tdat->compressed_chunk) {
			fprintf(stderr, "Out of memory\n");
			COMP_BAIL;
		}
		if (enable_rabin_scan)
			tdat->uncompressed_chunk = (uchar_t *)slab_alloc(NULL, compressed_chunksize + CHDR_SZ);
		else
			tdat->uncompressed_chunk = (uchar_t *)slab_alloc(NULL, chunksize);
		if (!tdat->uncompressed_chunk) {
			fprintf(stderr, "Out of memory\n");
			COMP_BAIL;
		}
		tdat->chunksize = chunksize;
		tdat->compress = _compress_func;
		tdat->decompress = _decompress_func;
		tdat->cancel = 0;
		tdat->level = level;
		sem_init(&(tdat->start_sem), 0, 0);
		sem_init(&(tdat->cmp_done_sem), 0, 0);
		sem_init(&(tdat->write_done_sem), 0, 1);
		if (_init_func) {
			if (_init_func(&(tdat->data), &(tdat->level), chunksize) != 0) {
				COMP_BAIL;
			}
		}
		if (enable_rabin_scan)
			tdat->rctx = create_rabin_context(chunksize, compressed_chunksize,
			    algo, enable_delta_encode);
		else
			tdat->rctx = NULL;

		if (pthread_create(&(tdat->thr), NULL, perform_compress,
		    (void *)tdat) != 0) {
			perror("Error in thread creation: ");
			COMP_BAIL;
		}
	}
	thread = 1;

	w.dary = dary;
	w.wfd = compfd;
	w.nprocs = nprocs;
	if (pthread_create(&writer_thr, NULL, writer_thread, (void *)(&w)) != 0) {
		perror("Error in thread creation: ");
		COMP_BAIL;
	}

	/*
	 * Write out file header. First insert hdr elements into mem buffer
	 * then write out the full hdr in one shot.
	 */
	memset(cread_buf, 0, ALGO_SZ);
	strncpy(cread_buf, algo, ALGO_SZ);
	version = htons(VERSION);
	flags = htons(flags);
	n_chunksize = htonll(chunksize);
	level = htonl(level);
	pos = cread_buf + ALGO_SZ;
	memcpy(pos, &version, sizeof (version));
	pos += sizeof (version);
	memcpy(pos, &flags, sizeof (flags));
	pos += sizeof (flags);
	memcpy(pos, &n_chunksize, sizeof (n_chunksize));
	pos += sizeof (n_chunksize);
	memcpy(pos, &level, sizeof (level));
	pos += sizeof (level);
	if (Write(compfd, cread_buf, pos - cread_buf) != pos - cread_buf) {
		perror("Write ");
		COMP_BAIL;
	}

	/*
	 * Now read from the uncompressed file in 'chunksize' sized chunks, independently
	 * compress each chunk and write it out. Chunk sequencing is ensured.
	 */
	chunk_num = 0;
	np = 0;
	bail = 0;
	largest_chunk = 0;
	smallest_chunk = chunksize;
	avg_chunk = 0;
	rabin_count = 0;

	/*
	 * Read the first chunk into a spare buffer (a simple double-buffering).
	 */
	if (enable_rabin_split) {
		rctx = create_rabin_context(chunksize, 0, algo, enable_delta_encode);
		rbytes = Read_Adjusted(uncompfd, cread_buf, chunksize, &rabin_count, rctx);
	} else {
		rbytes = Read(uncompfd, cread_buf, chunksize);
	}

	while (!bail) {
		uchar_t *tmp;

		if (main_cancel) break;
		for (p = 0; p < nprocs; p++) {
			np = p;
			tdat = dary[p];
			if (main_cancel) break;
			/* Wait for previous chunk compression to complete. */
			sem_wait(&tdat->write_done_sem);
			if (main_cancel) break;

			/*
			 * Once previous chunk is done swap already read buffer and
			 * it's size into the thread data.
			 * Normally it goes into uncompressed_chunk, because that's what it is.
			 * With dedup enabled however, we do some jugglery to save additional
			 * memory usage and avoid a memcpy, so it goes into the compressed_chunk
			 * area:
			 * cmp_seg -> dedup -> uncompressed_chunk -> compression -> cmp_seg
			 */
			tdat->id = chunk_num;
			tdat->rbytes = rbytes;
			if (enable_rabin_scan) {
				tmp = tdat->cmp_seg;
				tdat->cmp_seg = cread_buf;
				cread_buf = tmp;
				tdat->compressed_chunk = tdat->cmp_seg + sizeof (chunksize) + sizeof (uint64_t);

				/*
				 * If there is data after the last rabin boundary in the chunk, then
				 * rabin_count will be non-zero. We carry over the data to the beginning
				 * of the next chunk.
				 */
				if (rabin_count) {
					memcpy(cread_buf,
					    tdat->cmp_seg + rabin_count, rbytes - rabin_count);
					tdat->rbytes = rabin_count;
					rabin_count = rbytes - rabin_count;
				}
			} else {
				tmp = tdat->uncompressed_chunk;
				tdat->uncompressed_chunk = cread_buf;
				cread_buf = tmp;
			}
			if (rbytes < chunksize) {
				if (rbytes < 0) {
					bail = 1;
					perror("Read: ");
					COMP_BAIL;

				} else if (tdat->rbytes == 0) { /* EOF */
					bail = 1;
					break;
				}
			}
			/* Signal the compression thread to start */
			sem_post(&tdat->start_sem);
			chunk_num++;

			/*
			 * Read the next buffer we want to process while previous
			 * buffer is in progress.
			 */
			if (enable_rabin_split) {
				rbytes = Read_Adjusted(uncompfd, cread_buf, chunksize, &rabin_count, rctx);
			} else {
				rbytes = Read(uncompfd, cread_buf, chunksize);
			}
		}
	}

	if (!main_cancel) {
		/* Wait for all remaining chunks to finish. */
		for (p = 0; p < nprocs; p++) {
			if (p == np) continue;
			tdat = dary[p];
			sem_wait(&tdat->write_done_sem);
		}
	} else {
		err = 1;
	}

comp_done:
	if (thread) {
		for (i = 0; i < nprocs; i++) {
			tdat = dary[i];
			tdat->cancel = 1;
			tdat->len_cmp = 0;
			sem_post(&tdat->start_sem);
			sem_post(&tdat->cmp_done_sem);
			pthread_join(tdat->thr, NULL);
		}
		pthread_join(writer_thr, NULL);
	}

	if (err) {
		if (compfd != -1 && !pipe_mode)
			unlink(tmpfile1);
		fprintf(stderr, "Error compressing file: %s\n", filename);
	} else {
		/*
		* Write a trailer of zero chunk length.
		*/
		compressed_chunksize = 0;
		if (Write(compfd, &compressed_chunksize,
		    sizeof (compressed_chunksize)) < 0) {
			perror("Write ");
			err = 1;
		}

		/*
		 * Rename the temporary file to the actual compressed file
		 * unless we are in a pipe.
		 */
		if (!pipe_mode) {
			/*
			 * Ownership and mode of target should be same as original.
			 */
			fchmod(compfd, sbuf.st_mode);
			if (fchown(compfd, sbuf.st_uid, sbuf.st_gid) == -1)
				perror("chown ");

			if (rename(tmpfile1, to_filename) == -1) {
				perror("Cannot rename temporary file ");
				unlink(tmpfile1);
			}
		}
	}
	if (dary != NULL) {
		for (i = 0; i < nprocs; i++) {
			slab_free(NULL, dary[i]->uncompressed_chunk);
			slab_free(NULL, dary[i]->cmp_seg);
			if (enable_rabin_scan) {
				destroy_rabin_context(dary[i]->rctx);
			}
			if (_deinit_func)
				_deinit_func(&(dary[i]->data));
			slab_free(NULL, dary[i]);
		}
		slab_free(NULL, dary);
	}
	if (enable_rabin_split) destroy_rabin_context(rctx);
	slab_free(NULL, cread_buf);
	if (!pipe_mode) {
		if (compfd != -1) close(compfd);
		if (uncompfd != -1) close(uncompfd);
	}

	if (!hide_cmp_stats) show_compression_stats(chunksize);
	_stats_func(!hide_cmp_stats);
	slab_cleanup(hide_mem_stats);
}

/*
 * Check the algorithm requested and set the callback routine pointers.
 */
static int
init_algo(const char *algo, int bail)
{
	int rv = 1, i;
	char algorithm[8];

	/* Copy given string into known length buffer to avoid memcmp() overruns. */
	strncpy(algorithm, algo, 8);
	if (memcmp(algorithm, "zlib", 4) == 0) {
		_compress_func = zlib_compress;
		_decompress_func = zlib_decompress;
		_init_func = zlib_init;
		_deinit_func = NULL;
		_stats_func = zlib_stats;
		rv = 0;

	} else if (memcmp(algorithm, "lzma", 4) == 0) {
		_compress_func = lzma_compress;
		_decompress_func = lzma_decompress;
		_init_func = lzma_init;
		_deinit_func = lzma_deinit;
		_stats_func = lzma_stats;
		rv = 0;

	} else if (memcmp(algorithm, "bzip2", 5) == 0) {
		_compress_func = bzip2_compress;
		_decompress_func = bzip2_decompress;
		_init_func = bzip2_init;
		_deinit_func = NULL;
		_stats_func = bzip2_stats;
		rv = 0;

	} else if (memcmp(algorithm, "ppmd", 4) == 0) {
		_compress_func = ppmd_compress;
		_decompress_func = ppmd_decompress;
		_init_func = ppmd_init;
		_deinit_func = ppmd_deinit;
		_stats_func = ppmd_stats;
		rv = 0;

	} else if (memcmp(algorithm, "lzfx", 4) == 0) {
		_compress_func = lz_fx_compress;
		_decompress_func = lz_fx_decompress;
		_init_func = lz_fx_init;
		_deinit_func = lz_fx_deinit;
		_stats_func = lz_fx_stats;
		rv = 0;

	} else if (memcmp(algorithm, "lz4", 3) == 0) {
		_compress_func = lz4_compress;
		_decompress_func = lz4_decompress;
		_init_func = lz4_init;
		_deinit_func = lz4_deinit;
		_stats_func = lz4_stats;
		rv = 0;

	/* adapt2 and adapt ordering of the checks matter here. */
	} else if (memcmp(algorithm, "adapt2", 6) == 0) {
		_compress_func = adapt_compress;
		_decompress_func = adapt_decompress;
		_init_func = adapt2_init;
		_deinit_func = adapt_deinit;
		_stats_func = adapt_stats;
		adapt_mode = 1;
		rv = 0;

	} else if (memcmp(algorithm, "adapt", 5) == 0) {
		_compress_func = adapt_compress;
		_decompress_func = adapt_decompress;
		_init_func = adapt_init;
		_deinit_func = adapt_deinit;
		_stats_func = adapt_stats;
		adapt_mode = 1;
		rv = 0;
	}

	return (rv);
}

int
main(int argc, char *argv[])
{
	char *filename = NULL;
	char *to_filename = NULL;
	ssize_t chunksize = DEFAULT_CHUNKSIZE;
	int opt, level, num_rem;

	exec_name = get_execname(argv[0]);
	level = 6;
	slab_init();

	while ((opt = getopt(argc, argv, "dc:s:l:pt:MCDEr")) != -1) {
		int ovr;

		switch (opt) {
		    case 'd':
			do_uncompress = 1;
			break;

		    case 'c':
			do_compress = 1;
			algo = optarg;
			if (init_algo(algo, 1) != 0) {
				err_exit(1, "Invalid algorithm %s\n", optarg);
			}
			break;

		    case 's':
			ovr = parse_numeric(&chunksize, optarg);
			if (ovr == 1)
				err_exit(0, "Chunk size too large %s", optarg);
			else if (ovr == 2)
				err_exit(0, "Invalid number %s", optarg);

			if (chunksize < MIN_CHUNK) {
				err_exit(0, "Minimum chunk size is %ld\n", MIN_CHUNK);
			}
			break;

		    case 'l':
			level = atoi(optarg);
			if (level < 0 || level > 14)
				err_exit(0, "Compression level should be in range 0 - 14\n");
			break;

		    case 'p':
			pipe_mode = 1;
			break;

		    case 't':
			nthreads = atoi(optarg);
			if (nthreads < 1 || nthreads > 256)
				err_exit(0, "Thread count should be in range 1 - 256\n");
			break;

		    case 'M':
			hide_mem_stats = 0;
			break;

		    case 'C':
			hide_cmp_stats = 0;
			break;

		    case 'D':
			enable_rabin_scan = 1;
			break;

		    case 'E':
			enable_rabin_scan = 1;
			enable_delta_encode = 1;
			break;

		    case 'r':
			enable_rabin_split = 0;
			break;

		    case '?':
		    default:
			usage();
			exit(1);
			break;
		}
	}

	if ((do_compress && do_uncompress) || (!do_compress && !do_uncompress)) {
		usage();
		exit(1);
	}

	/*
	 * Remaining mandatory arguments are the filenames.
	 */
	num_rem = argc - optind;
	if (pipe_mode && num_rem > 0 ) {
		fprintf(stderr, "Filename(s) unexpected for pipe mode\n");
		usage();
		exit(1);
	}

	if (enable_rabin_scan && !do_compress) {
		fprintf(stderr, "Rabin Deduplication is only used during compression.\n");
		usage();
		exit(1);
	}
	if (!enable_rabin_scan)
		enable_rabin_split = 0;

	if (num_rem == 0 && !pipe_mode) {
		usage(); /* At least 1 filename needed. */
		exit(1);

	} else if (num_rem == 1) {
		if (do_compress) {
			char apath[MAXPATHLEN];

			if ((filename = realpath(argv[optind], NULL)) == NULL)
				err_exit(1, "%s", argv[optind]);
			/* Check if compressed file exists */
			strcpy(apath, filename);
			strcat(apath, COMP_EXTN);
			if ((to_filename = realpath(apath, NULL)) != NULL) {
				free(filename);
				err_exit(0, "Compressed file %s exists\n", to_filename);
			}
		} else {
			usage();
			exit(1);
		}
	} else if (num_rem == 2) {
		if (do_uncompress) {
			if ((filename = realpath(argv[optind], NULL)) == NULL)
				err_exit(1, "%s", argv[optind]);
			optind++;
			if ((to_filename = realpath(argv[optind], NULL)) != NULL) {
				free(filename);
				free(to_filename);
				err_exit(0, "File %s exists\n", argv[optind]);
			}
			to_filename = argv[optind];
		} else {
			usage();
			exit(1);
		}
	} else if (num_rem > 2) {
		fprintf(stderr, "Too many filenames.\n");
		usage();
		exit(1);
	}
	main_cancel = 0;

	/*
	 * Start the main routines.
	 */
	if (do_compress)
		start_compress(filename, chunksize, level);
	else if (do_uncompress)
		start_decompress(filename, to_filename);

	free(filename);
	free((void *)exec_name);
	return (0);
}
