/*
 * The rabin polynomial computation is derived from:
 * http://code.google.com/p/rabin-fingerprint-c/
 * 
 * originally created by Joel Lawrence Tucci on 09-March-2011.
 * 
 * Rabin polynomial portions Copyright (c) 2011 Joel Lawrence Tucci
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the project's author nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 * 
 */

#ifndef __STDC_FORMAT_MACROS
#define	__STDC_FORMAT_MACROS	1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <allocator.h>
#include <utils.h>
#include <pthread.h>
#include <heapq.h>
#include <xxhash.h>
#include <blake2_digest.h>

#include "rabin_dedup.h"
#if defined(__USE_SSE_INTRIN__) && defined(__SSE4_1__) && RAB_POLYNOMIAL_WIN_SIZE == 16
#	include <smmintrin.h>
#	define	SSE_MODE		1
#endif

#if defined(_OPENMP)
#include <omp.h>
#endif

#define	DELTA_EXTRA2_PCT(x) ((x) >> 1)
#define	DELTA_EXTRA_PCT(x) (((x) >> 1) + ((x) >> 3))
#define	DELTA_NORMAL_PCT(x) (((x) >> 1) + ((x) >> 2) + ((x) >> 3))

extern int lzma_init(void **data, int *level, int nthreads, int64_t chunksize,
		     int file_version, compress_op_t op);
extern int lzma_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *destlen, int level, uchar_t chdr, void *data);
extern int lzma_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, void *data);
extern int lzma_deinit(void **data);
extern int bsdiff(u_char *oldbuf, bsize_t oldsize, u_char *newbuf, bsize_t newsize,
       u_char *diff, u_char *scratch, bsize_t scratchsize);
extern bsize_t get_bsdiff_sz(u_char *pbuf);
extern int bspatch(u_char *pbuf, u_char *oldbuf, bsize_t oldsize, u_char *newbuf,
	bsize_t *_newsize);
extern uint64_t lzma_crc64(const uint8_t *buf, size_t size, uint64_t crc);

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
uint64_t ir[256], out[256];
static int inited = 0;
archive_config_t *arc = NULL;
static struct blake2_dispatch bdsp;
int seg = 0;

static uint32_t
dedupe_min_blksz(int rab_blk_sz)
{
	uint32_t min_blk;

	min_blk = (1 << (rab_blk_sz + RAB_BLK_MIN_BITS)) - 1024;
	return (min_blk);
}

uint32_t
dedupe_buf_extra(uint64_t chunksize, int rab_blk_sz, const char *algo, int delta_flag)
{
	if (rab_blk_sz < 1 || rab_blk_sz > 5)
		rab_blk_sz = RAB_BLK_DEFAULT;

	return ((chunksize / dedupe_min_blksz(rab_blk_sz)) * sizeof (uint32_t));
}

/*
 * Helper function to let caller size the the user specific compression chunk/segment
 * to align with deduplication requirements.
 */
int
global_dedupe_bufadjust(uint32_t rab_blk_sz, uint64_t *user_chunk_sz, int pct_interval,
		 const char *algo, cksum_t ck, cksum_t ck_sim, size_t file_sz,
		 size_t memlimit, int nthreads)
{
	uint64_t memreqd;
	archive_config_t cfg;
	int rv, pct_i, hash_entry_size;
	uint32_t hash_slots;

	rv = 0;
	pct_i = pct_interval;
	rv = setup_db_config_s(&cfg, rab_blk_sz, user_chunk_sz, &pct_i, algo, ck, ck_sim,
		 file_sz, &hash_slots, &hash_entry_size, &memreqd, memlimit, "/tmp");
	return (rv);
}

/*
 * Initialize the algorithm with the default params.
 */
dedupe_context_t *
create_dedupe_context(uint64_t chunksize, uint64_t real_chunksize, int rab_blk_sz,
    const char *algo, const algo_props_t *props, int delta_flag, int dedupe_flag,
    int file_version, compress_op_t op, uint64_t file_size, char *tmppath) {
	dedupe_context_t *ctx;
	uint32_t i;

	if (rab_blk_sz < 1 || rab_blk_sz > 5)
		rab_blk_sz = RAB_BLK_DEFAULT;

	if (dedupe_flag == RABIN_DEDUPE_FIXED || dedupe_flag == RABIN_DEDUPE_FILE_GLOBAL) {
		delta_flag = 0;
		if (dedupe_flag != RABIN_DEDUPE_FILE_GLOBAL)
			inited = 1;
	}

	/*
	 * Pre-compute a table of irreducible polynomial evaluations for each
	 * possible byte value.
	 */
	pthread_mutex_lock(&init_lock);
	if (!inited) {
		int term, pow, j;
		uint64_t val, poly_pow;

		poly_pow = 1;
		for (j = 0; j < RAB_POLYNOMIAL_WIN_SIZE; j++) {
			poly_pow = (poly_pow * RAB_POLYNOMIAL_CONST) & POLY_MASK;
		}

		for (j = 0; j < 256; j++) {
			term = 1;
			pow = 1;
			val = 1;
			out[j] = (j * poly_pow) & POLY_MASK;
			for (i=0; i<RAB_POLYNOMIAL_WIN_SIZE; i++) {
				if (term & FP_POLY) {
					val += ((pow * j) & POLY_MASK);
				}
				pow = (pow * RAB_POLYNOMIAL_CONST) & POLY_MASK;
				term <<= 1;
			}
			ir[j] = val;
		}

		/*
		 * If Global Deduplication is enabled initialize the in-memory index.
		 * It is essentially a hashtable that is used for crypto-hash based
		 * chunk matching.
		 */
		if (dedupe_flag == RABIN_DEDUPE_FILE_GLOBAL && op == COMPRESS && rab_blk_sz > 0) {
			my_sysinfo msys_info;

			/*
			 * Get amount of memory to use. The freeram got here is adjusted amount.
			 */
			get_sys_limits(&msys_info);

			arc = init_global_db_s(NULL, tmppath, rab_blk_sz, chunksize, 0,
					      algo, props->cksum, GLOBAL_SIM_CKSUM, file_size,
					      msys_info.freeram, props->nthreads);
			if (arc == NULL) {
				pthread_mutex_unlock(&init_lock);
				return (NULL);
			}
			blake2_module_init(&bdsp, &proc_info);
		}
		inited = 1;
	}
	pthread_mutex_unlock(&init_lock);

	/*
	 * Rabin window size must be power of 2 for optimization.
	 */
	if (!ISP2(RAB_POLYNOMIAL_WIN_SIZE)) {
		fprintf(stderr, "Rabin window size must be a power of 2 in range 4 <= x <= 64\n");
		return (NULL);
	}

	if (chunksize < RAB_MIN_CHUNK_SIZE) {
		fprintf(stderr, "Minimum chunk size for Dedup must be %" PRIu64 " bytes\n",
		    RAB_MIN_CHUNK_SIZE);
		return (NULL);
	}

	/*
	 * For LZMA with chunksize <= LZMA Window size and/or Delta enabled we
	 * use 4K minimum Rabin block size. For everything else it is 2K based
	 * on experimentation.
	 */
	ctx = (dedupe_context_t *)slab_alloc(NULL, sizeof (dedupe_context_t));
	ctx->rabin_poly_max_block_size = RAB_POLYNOMIAL_MAX_BLOCK_SIZE;
	ctx->arc = arc;

	ctx->current_window_data = NULL;
	ctx->dedupe_flag = dedupe_flag;
	ctx->rabin_break_patt = 0;
	ctx->rabin_poly_avg_block_size = RAB_BLK_AVG_SZ(rab_blk_sz);
	ctx->rabin_avg_block_mask = RAB_BLK_MASK;
	ctx->rabin_poly_min_block_size = dedupe_min_blksz(rab_blk_sz);
	ctx->delta_flag = 0;
	ctx->deltac_min_distance = props->deltac_min_distance;
	ctx->pagesize = sysconf(_SC_PAGE_SIZE);
	ctx->similarity_cksums = NULL;
	if (arc)
		arc->pagesize = ctx->pagesize;

	/*
	 * Scale down similarity percentage based on avg block size unless user specified
	 * argument '-EE' in which case fixed 40% match is used for Delta compression.
	 */
	if (delta_flag == DELTA_NORMAL) {
		if (ctx->rabin_poly_avg_block_size < (1 << 14)) {
			ctx->delta_flag = 1;
		} else if (ctx->rabin_poly_avg_block_size < (1 << 16)) {
			ctx->delta_flag = 2;
		} else {
			ctx->delta_flag = 3;
		}
	} else if (delta_flag == DELTA_EXTRA) {
		ctx->delta_flag = 2;
	}

	if (dedupe_flag != RABIN_DEDUPE_FIXED)
		ctx->blknum = chunksize / ctx->rabin_poly_min_block_size;
	else
		ctx->blknum = chunksize / ctx->rabin_poly_avg_block_size;

	if (chunksize % ctx->rabin_poly_min_block_size)
		++(ctx->blknum);

	if (ctx->blknum > RABIN_MAX_BLOCKS) {
		fprintf(stderr, "Chunk size too large for dedup.\n");
		destroy_dedupe_context(ctx);
		return (NULL);
	}
#ifndef SSE_MODE
	ctx->current_window_data = (uchar_t *)slab_alloc(NULL, RAB_POLYNOMIAL_WIN_SIZE);
#else
	ctx->current_window_data = (uchar_t *)1;
#endif
	ctx->blocks = NULL;
	if (real_chunksize > 0 && dedupe_flag != RABIN_DEDUPE_FILE_GLOBAL) {
		ctx->blocks = (rabin_blockentry_t **)slab_calloc(NULL,
			ctx->blknum, sizeof (rabin_blockentry_t *));
	}
	if(ctx == NULL || ctx->current_window_data == NULL ||
	    (ctx->blocks == NULL && real_chunksize > 0 && dedupe_flag != RABIN_DEDUPE_FILE_GLOBAL)) {
		fprintf(stderr,
		    "Could not allocate rabin polynomial context, out of memory\n");
		destroy_dedupe_context(ctx);
		return (NULL);
	}

	if (arc && dedupe_flag == RABIN_DEDUPE_FILE_GLOBAL) {
		ctx->similarity_cksums = (uchar_t *)slab_calloc(NULL,
					arc->sub_intervals,
					arc->similarity_cksum_sz);
		if (!ctx->similarity_cksums) {
			fprintf(stderr,
			    "Could not allocate dedupe context, out of memory\n");
			destroy_dedupe_context(ctx);
			return (NULL);
		}
	}

	ctx->lzma_data = NULL;
	ctx->level = 14;
	if (real_chunksize > 0) {
		lzma_init(&(ctx->lzma_data), &(ctx->level), 1, chunksize, file_version, op);

		// The lzma_data member is not needed during decompression
		if (!(ctx->lzma_data) && op == COMPRESS) {
			fprintf(stderr,
			    "Could not initialize LZMA data for dedupe index, out of memory\n");
			destroy_dedupe_context(ctx);
			return (NULL);
		}
	}

	slab_cache_add(sizeof (rabin_blockentry_t));
	ctx->real_chunksize = real_chunksize;
	reset_dedupe_context(ctx);
	return (ctx);
}

void
reset_dedupe_context(dedupe_context_t *ctx)
{
#ifndef	SSE_MODE
	memset(ctx->current_window_data, 0, RAB_POLYNOMIAL_WIN_SIZE);
#endif
	ctx->valid = 0;
}

void
destroy_dedupe_context(dedupe_context_t *ctx)
{
	if (ctx) {
		uint32_t i;
#ifndef SSE_MODE
		if (ctx->current_window_data) slab_free(NULL, ctx->current_window_data);
#endif

		pthread_mutex_lock(&init_lock);
		if (arc) {
			destroy_global_db_s(arc);
		}
		arc = NULL;
		pthread_mutex_unlock(&init_lock);

		if (ctx->blocks) {
			for (i=0; i<ctx->blknum && ctx->blocks[i] != NULL; i++) {
				slab_free(NULL, ctx->blocks[i]);
			}
			slab_free(NULL, ctx->blocks);
		}
		if (ctx->similarity_cksums) slab_free(NULL, ctx->similarity_cksums);
		if (ctx->lzma_data) lzma_deinit(&(ctx->lzma_data));
		slab_free(NULL, ctx);
	}
}

int
cmpint(const void *a, const void *b)
{
	uint64_t a1 = *((uint64_t *)a);
	uint64_t b1 = *((uint64_t *)b);

	if (a1 < b1)
		return (-1);
	else if (a1 == b1)
		return (0);
	else
		return (1);
}

static inline int
ckcmp(uchar_t *a, uchar_t *b, int sz)
{
	size_t *v1 = (size_t *)a;
	size_t *v2 = (size_t *)b;
	int len;

	len = 0;
	do {
		if (*v1 != *v2) {
			return (1);
		}
		++v1;
		++v2;
		len += sizeof (size_t);
	} while (len < sz);

	return (0);
}

/**
 * Perform Deduplication.
 * Both Semi-Rabin fingerprinting based and Fixed Block Deduplication are supported.
 * A 16-byte window is used for the rolling checksum and dedup blocks can vary in size
 * from 4K-128K.
 */
uint32_t
dedupe_compress(dedupe_context_t *ctx, uchar_t *buf, uint64_t *size, uint64_t offset,
		uint64_t *rabin_pos, int mt)
{
	uint64_t i, last_offset, j, ary_sz;
	uint32_t blknum, window_pos;
	uchar_t *buf1 = (uchar_t *)buf;
	uint32_t length;
	uint64_t cur_roll_checksum, cur_pos_checksum;
	uint32_t *ctx_heap;
	rabin_blockentry_t **htab;
	heap_t heap;
	DEBUG_STAT_EN(uint32_t max_count);
	DEBUG_STAT_EN(max_count = 0);
	DEBUG_STAT_EN(double strt, en_1, en);

	length = offset;
	last_offset = 0;
	blknum = 0;
	window_pos = 0;
	ctx->valid = 0;
	cur_roll_checksum = 0;
	if (*size < ctx->rabin_poly_avg_block_size) return (0);
	DEBUG_STAT_EN(strt = get_wtime_millis());

	if (ctx->dedupe_flag == RABIN_DEDUPE_FIXED) {
		blknum = *size / ctx->rabin_poly_avg_block_size;
		j = *size % ctx->rabin_poly_avg_block_size;
		if (j)
			++blknum;
		else
			j = ctx->rabin_poly_avg_block_size;

		last_offset = 0;
		length = ctx->rabin_poly_avg_block_size;
		for (i=0; i<blknum; i++) {
			if (i == blknum-1) {
				length = j;
			}
			if (ctx->blocks[i] == 0) {
				ctx->blocks[i] = (rabin_blockentry_t *)slab_alloc(NULL,
				    sizeof (rabin_blockentry_t));
			}
			ctx->blocks[i]->offset = last_offset;
			ctx->blocks[i]->index = i; // Need to store for sorting
			ctx->blocks[i]->length = length;
			ctx->blocks[i]->similar = 0;
			ctx->blocks[i]->hash = XXH32(buf1+last_offset, length, 0);
			ctx->blocks[i]->similarity_hash = ctx->blocks[i]->hash;
			last_offset += length;
		}
		goto process_blocks;
	}

	if (rabin_pos == NULL) {
		/*
		 * If global dedupe is active, the global blocks array uses temp space in
		 * the target buffer.
		 */
		ary_sz = 0;
		if (ctx->arc != NULL) {
			ary_sz = (sizeof (global_blockentry_t) * (*size / ctx->rabin_poly_min_block_size + 1));
			ctx->g_blocks = (global_blockentry_t *)(ctx->cbuf + ctx->real_chunksize - ary_sz);
		}

		/*
		 * Initialize arrays for sketch computation. We re-use memory allocated
		 * for the compressed chunk temporarily.
		 */
		ary_sz += ctx->rabin_poly_max_block_size;
		ctx_heap = (uint32_t *)(ctx->cbuf + ctx->real_chunksize - ary_sz);
	}
#ifndef SSE_MODE
	memset(ctx->current_window_data, 0, RAB_POLYNOMIAL_WIN_SIZE);
#else
	__m128i cur_sse_byte = _mm_setzero_si128();
	__m128i window = _mm_setzero_si128();
#endif
	j = *size - RAB_POLYNOMIAL_WIN_SIZE;

	/* 
	 * If rabin_pos is non-zero then we are being asked to scan for the last rabin boundary
	 * in the chunk. We start scanning at chunk end - max rabin block size. We avoid doing
	 * a full chunk scan.
	 * 
	 * !!!NOTE!!!: Code duplication below for performance.
	 */
	if (rabin_pos) {
		offset = *size - ctx->rabin_poly_max_block_size;
		length = 0;
		for (i=offset; i<j; i++) {
			int cur_byte = buf1[i];
#ifdef	SSE_MODE
			uint32_t pushed_out = _mm_extract_epi32(window, 3);
			pushed_out >>= 24;
			asm ("movd %[cur_byte], %[cur_sse_byte]"
			     : [cur_sse_byte] "=x" (cur_sse_byte)
			     : [cur_byte] "r" (cur_byte)
			);
			window = _mm_slli_si128(window, 1);
			window = _mm_or_si128(window, cur_sse_byte);
#else
			uint32_t pushed_out = ctx->current_window_data[window_pos];
			ctx->current_window_data[window_pos] = cur_byte;
#endif

			cur_roll_checksum = (cur_roll_checksum * RAB_POLYNOMIAL_CONST) & POLY_MASK;
			cur_roll_checksum += cur_byte;
			cur_roll_checksum -= out[pushed_out];

#ifndef	SSE_MODE
			window_pos = (window_pos + 1) & (RAB_POLYNOMIAL_WIN_SIZE-1);
#endif
			++length;
			if (length < ctx->rabin_poly_min_block_size) continue;

			// If we hit our special value update block offset
			cur_pos_checksum = cur_roll_checksum ^ ir[pushed_out];
			if ((cur_pos_checksum & ctx->rabin_avg_block_mask) == ctx->rabin_break_patt) {
				last_offset = i;
				length = 0;
			}
		}

		if (last_offset < *size) {
			*rabin_pos = last_offset;
		}
		return (0);
	}

	/*
	 * Start our sliding window at a fixed number of bytes before the min window size.
	 * It is pointless to slide the window over the whole length of the chunk.
	 */
	offset = ctx->rabin_poly_min_block_size - RAB_WINDOW_SLIDE_OFFSET;
	length = offset;
	for (i=offset; i<j; i++) {
		uint64_t pc[4];
		uint32_t cur_byte = buf1[i];

#ifdef	SSE_MODE
		/*
		 * A 16-byte XMM register is used as a sliding window if our window size is 16 bytes
		 * and at least SSE 4.1 is enabled. Avoids memory access for the sliding window.
		 */
		uint32_t pushed_out = _mm_extract_epi32(window, 3);
		pushed_out >>= 24;

		/*
		 * No intrinsic available for this.
		 */
		asm ("movd %[cur_byte], %[cur_sse_byte]"
		     : [cur_sse_byte] "=x" (cur_sse_byte)
		     : [cur_byte] "r" (cur_byte)
		);
		window = _mm_slli_si128(window, 1);
		window = _mm_or_si128(window, cur_sse_byte);
#else
		uint32_t pushed_out = ctx->current_window_data[window_pos];
		ctx->current_window_data[window_pos] = cur_byte;
#endif

		cur_roll_checksum = (cur_roll_checksum * RAB_POLYNOMIAL_CONST) & POLY_MASK;
		cur_roll_checksum += cur_byte;
		cur_roll_checksum -= out[pushed_out];

#ifndef	SSE_MODE
		/*
		 * Window pos has to rotate from 0 .. RAB_POLYNOMIAL_WIN_SIZE-1
		 * We avoid a branch here by masking. This requires RAB_POLYNOMIAL_WIN_SIZE
		 * to be power of 2
		 */
		window_pos = (window_pos + 1) & (RAB_POLYNOMIAL_WIN_SIZE-1);
#endif
		++length;
		if (length < ctx->rabin_poly_min_block_size) continue;

		// If we hit our special value or reached the max block size update block offset
		cur_pos_checksum = cur_roll_checksum ^ ir[pushed_out];
		if ((cur_pos_checksum & ctx->rabin_avg_block_mask) == ctx->rabin_break_patt ||
		    length >= ctx->rabin_poly_max_block_size) {

			if (!(ctx->arc)) {
				if (ctx->blocks[blknum] == 0)
					ctx->blocks[blknum] = (rabin_blockentry_t *)slab_alloc(NULL,
					    sizeof (rabin_blockentry_t));
				ctx->blocks[blknum]->offset = last_offset;
				ctx->blocks[blknum]->index = blknum; // Need to store for sorting
				ctx->blocks[blknum]->length = length;
			} else {
				ctx->g_blocks[blknum].length = length;
				ctx->g_blocks[blknum].offset = last_offset;
			}
			DEBUG_STAT_EN(if (length >= ctx->rabin_poly_max_block_size) ++max_count);

			/*
			 * Reset the heap structure and find the K min values if Delta Compression
			 * is enabled. We use a min heap mechanism taken from the heap based priority
			 * queue implementation in Python.
			 * Here K = similarity extent = 87% or 62% or 50%.
			 * 
			 * Once block contents are arranged in a min heap we compute the K min values
			 * sketch by hashing over the heap till K%. We interpret the raw bytes as a
			 * sequence of 64-bit integers.
			 * This is variant of minhashing which is used widely, for example in various
			 * search engines to detect similar documents.
			 */
			if (ctx->delta_flag) {
				memcpy(ctx_heap, buf1+last_offset, length);
				length /= 8;
				pc[1] = DELTA_NORMAL_PCT(length);
				pc[2] = DELTA_EXTRA_PCT(length);
				pc[3] = DELTA_EXTRA2_PCT(length);

				reset_heap(&heap, pc[ctx->delta_flag]);
				ksmallest((int64_t *)ctx_heap, length, &heap);

				ctx->blocks[blknum]->similarity_hash =
					XXH32((const uchar_t *)ctx_heap,  pc[ctx->delta_flag]*8, 0);
			}
			++blknum;
			last_offset = i+1;
			length = 0;
			if (*size - last_offset <= ctx->rabin_poly_min_block_size) break;
			length = ctx->rabin_poly_min_block_size - RAB_WINDOW_SLIDE_OFFSET;
			i = i + length;
		}
	}

	// Insert the last left-over trailing bytes, if any, into a block.
	if (last_offset < *size) {
		length = *size - last_offset;
		if (!(ctx->arc)) {
			if (ctx->blocks[blknum] == 0)
				ctx->blocks[blknum] = (rabin_blockentry_t *)slab_alloc(NULL,
					sizeof (rabin_blockentry_t));
			ctx->blocks[blknum]->offset = last_offset;
			ctx->blocks[blknum]->index = blknum;
			ctx->blocks[blknum]->length = length;
		} else {
			ctx->g_blocks[blknum].length = length;
			ctx->g_blocks[blknum].offset = last_offset;
		}

		if (ctx->delta_flag) {
			uint64_t cur_sketch;
			uint64_t pc[4];

			if (length > ctx->rabin_poly_min_block_size) {
				memcpy(ctx_heap, buf1+last_offset, length);
				length /= 8;
				pc[1] = DELTA_NORMAL_PCT(length);
				pc[2] = DELTA_EXTRA_PCT(length);
				pc[3] = DELTA_EXTRA2_PCT(length);

				reset_heap(&heap, pc[ctx->delta_flag]);
				ksmallest((int64_t *)ctx_heap, length, &heap);
				cur_sketch =
				    XXH32((const uchar_t *)ctx_heap,  pc[ctx->delta_flag]*8, 0);
			} else {
				cur_sketch =
				    XXH32((const uchar_t *)(buf1+last_offset), length, 0);
			}
			ctx->blocks[blknum]->similarity_hash = cur_sketch;
		}
		++blknum;
		last_offset = *size;
	}

process_blocks:
	// If we found at least a few chunks, perform dedup.
	DEBUG_STAT_EN(en_1 = get_wtime_millis());
	DEBUG_STAT_EN(fprintf(stderr, "Original size: %" PRId64 ", blknum: %u\n", *size, blknum));
	DEBUG_STAT_EN(fprintf(stderr, "Number of maxlen blocks: %u\n", max_count));
	if (blknum > 2) {
		uint64_t pos, matchlen, pos1 = 0;
		int valid = 1;
		uint32_t *dedupe_index;
		uint64_t dedupe_index_sz = 0;
		rabin_blockentry_t *be;
		DEBUG_STAT_EN(uint32_t delta_calls, delta_fails, merge_count, hash_collisions);
		DEBUG_STAT_EN(double w1 = 0);
		DEBUG_STAT_EN(double w2 = 0);
		DEBUG_STAT_EN(delta_calls = 0);
		DEBUG_STAT_EN(delta_fails = 0);
		DEBUG_STAT_EN(hash_collisions = 0);

		/*
		 * If global dedupe is enabled then process it here.
		 */
		if (ctx->arc) {
			uchar_t *g_dedupe_idx, *tgt, *src;

			/*
			 * First compute all the rabin chunk/block cryptographic hashes.
			 */
#if defined(_OPENMP)
#	pragma omp parallel for if (mt)
#endif
			for (i=0; i<blknum; i++) {
				compute_checksum(ctx->g_blocks[i].cksum,
					ctx->arc->chunk_cksum_type, buf1+ctx->g_blocks[i].offset,
					ctx->g_blocks[i].length, 0, 0);
			}

			/*
			 * Index table within this segment.
			 */
			g_dedupe_idx = ctx->cbuf + RABIN_HDR_SIZE;
			dedupe_index_sz = 0;

			/*
			 * First entry in table is the original file offset where this
			 * data segment begins.
			 */
			*((uint64_t *)g_dedupe_idx) = LE64(ctx->file_offset);
			g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);
			dedupe_index_sz += 2;
			matchlen = 0;

			if (ctx->arc->dedupe_mode == MODE_SIMPLE) {
				/*======================================================================
				 * This code block implements Global Dedupe with simple in-memory index.
				 *======================================================================
				 */
				/*
				 * Now lookup blocks in index. First wait for our semaphore to be
				 * signaled. If the previous thread in sequence is using the index
				 * he will finish and then signal our semaphore. So we can have
				 * predictable serialization of index access in a sequence of
				 * threads without locking.
				 */
				length = 0;
				DEBUG_STAT_EN(w1 = get_wtime_millis());
				sem_wait(ctx->index_sem);
				DEBUG_STAT_EN(w2 = get_wtime_millis());
				for (i=0; i<blknum; i++) {
					hash_entry_t *he;

					he = db_lookup_insert_s(ctx->arc, ctx->g_blocks[i].cksum, 0,
						ctx->file_offset + ctx->g_blocks[i].offset,
						ctx->g_blocks[i].length, 1);
					if (!he) {
						/*
						 * Block match in index not found.
						 * Block was added to index. Merge this block.
						 */
						if (length + ctx->g_blocks[i].length > RABIN_MAX_BLOCK_SIZE) {
							*((uint32_t *)g_dedupe_idx) = LE32(length);
							g_dedupe_idx += RABIN_ENTRY_SIZE;
							length = 0;
							dedupe_index_sz++;
						}
						length += ctx->g_blocks[i].length;
					} else {
						/*
						 * Block match in index was found.
						 */
						if (length > 0) {
							/*
							 * Write pending accumulated block length value.
							 */
							*((uint32_t *)g_dedupe_idx) = LE32(length);
							g_dedupe_idx += RABIN_ENTRY_SIZE;
							length = 0;
							dedupe_index_sz++;
						}
						/*
						 * Add a reference entry to the dedupe array.
						 */
						*((uint32_t *)g_dedupe_idx) = LE32((he->item_size | RABIN_INDEX_FLAG) &
							CLEAR_SIMILARITY_FLAG);
						g_dedupe_idx += RABIN_ENTRY_SIZE;
						*((uint64_t *)g_dedupe_idx) = LE64(he->item_offset);
						g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);
						matchlen += he->item_size;
						dedupe_index_sz += 3;
					}
				}

				/*
				 * Signal the next thread in sequence to access the index.
				 */
				sem_post(ctx->index_sem_next);

				/*
				 * Write final pending block length value (if any).
				 */
				if (length > 0) {
					*((uint32_t *)g_dedupe_idx) = LE32(length);
					g_dedupe_idx += RABIN_ENTRY_SIZE;
					length = 0;
					dedupe_index_sz++;
				}

				blknum = dedupe_index_sz; // Number of entries in block list
				tgt = g_dedupe_idx;
				g_dedupe_idx = ctx->cbuf + RABIN_HDR_SIZE;
				dedupe_index_sz = tgt - g_dedupe_idx;
				src = buf1;
				g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);

			} else {
				uchar_t *seg_heap, *sim_ck;
				archive_config_t *cfg;
				uint32_t increment, len, blks, o_blks, k;
				global_blockentry_t *seg_blocks;
				uint64_t seg_offset, offset;
				global_blockentry_t **htab, *be;
				int sub_i;

				/*======================================================================
				 * This code block implements Segmented similarity based Dedupe with
				 * in-memory index for very large datasets.
				 * ======================================================================
				 */
				cfg = ctx->arc;
				seg_heap = (uchar_t *)(ctx->g_blocks) - cfg->segment_sz * cfg->chunk_cksum_sz;
				ary_sz = cfg->segment_sz * sizeof (global_blockentry_t **);
				htab = (global_blockentry_t **)(seg_heap - ary_sz);
				for (i=0; i<blknum;) {
					uint64_t crc, off1;
					length = 0;

					/*
					 * Compute length of current segment.
					 */
					blks = cfg->segment_sz;
					if (blks > blknum-i) blks = blknum-i;
					length = 0;
					tgt = seg_heap;
					for (j=0; j<blks; j++) {
						memcpy(tgt, ctx->g_blocks[j+i].cksum, cfg->chunk_cksum_sz);
						length += cfg->chunk_cksum_sz;
						tgt += cfg->chunk_cksum_sz;
					}
					blks = j+i;

					/*
					 * Sort concatenated chunk hash buffer by raw 64-bit integer
					 * magnitudes.
					 */
					qsort(seg_heap, length/8, 8, cmpint);

					/*
					 * Compute the min-values range similarity hashes.
					 */
					sim_ck = ctx->similarity_cksums;
					sub_i = cfg->sub_intervals;
					len = length;
					tgt = seg_heap;
					increment = cfg->chunk_cksum_sz;
					if  (increment * sub_i > len)
						sub_i = len / increment;
					for (j = 0; j<sub_i; j++) {
						crc = lzma_crc64(tgt, increment/4, 0);
						*((uint64_t *)sim_ck) = crc;
						tgt += increment;
						len -= increment;
						sim_ck += cfg->similarity_cksum_sz;
					}

					/*
					 * Begin shared index access and write segment metadata to cache
					 * first.
					 */
					if (i == 0) {
						DEBUG_STAT_EN(w1 = get_wtime_millis());
						sem_wait(ctx->index_sem);
						DEBUG_STAT_EN(w2 = get_wtime_millis());
					}

					sim_ck -= cfg->similarity_cksum_sz;
					seg_offset = db_segcache_pos(cfg, ctx->id);
					src = (uchar_t *)&(ctx->g_blocks[i]);
					len = blks * sizeof (global_blockentry_t);
					db_segcache_write(cfg, ctx->id, src, len, blks-i, ctx->file_offset);

					/*
					 * Insert current segment blocks into local hashtable and do partial
					 * in-segment deduplication.
					 */
					be = NULL;
					memset(htab, 0, ary_sz);
					for (k=i; k<blks; k++) {
						uint32_t hent;
						hent = XXH32(ctx->g_blocks[k].cksum, cfg->chunk_cksum_sz, 0);
						hent ^= (hent / cfg->chunk_cksum_sz);
						hent = hent % cfg->segment_sz;

						if (htab[hent] == NULL) {
							htab[hent] = &(ctx->g_blocks[k]);
							ctx->g_blocks[k].offset += ctx->file_offset;
							ctx->g_blocks[k].next = NULL;
							be = NULL;
						} else {
							be = htab[hent];
							do {
								if (ckcmp(ctx->g_blocks[k].cksum,
								    be->cksum, cfg->chunk_cksum_sz) == 0 &&
								    ctx->g_blocks[k].length == be->length) {
									global_blockentry_t *en;

									/*
									 * Block match in index was found. Update g_blocks
									 * array.
									 */
									en = &(ctx->g_blocks[k]);
									en->length = (en->length | RABIN_INDEX_FLAG) &
										CLEAR_SIMILARITY_FLAG;
									en->offset = be->offset;
									break;
								}
								if (be->next) {
									be = be->next;
								} else {
									be->next = &(ctx->g_blocks[k]);
									be->next->offset += ctx->file_offset;
									be->next->next = NULL;
									break;
								}
							} while(1);
						}
					}

					/*
					 * Now lookup all the similarity hashes. We sort the hashes first so that
					 * all duplicate hash values can be easily eliminated.
					 */
					qsort(ctx->similarity_cksums, sub_i, 8, cmpint);
					crc = 0;
					off1 = UINT64_MAX;
					for (j=sub_i; j > 0; j--) {
						hash_entry_t *he = NULL;

						/*
						 * Check for duplicate checksum which need not be looked up
						 * again.
						 */
						if (crc == *((uint64_t *)sim_ck)) {
							he = NULL;
						} else {
							he = db_lookup_insert_s(cfg, sim_ck, 0, seg_offset, 0, 1);
							/*
							 * Check for different checksum but same segment match.
							 * This is not a complete check but does help to reduce
							 * wasted processing.
							 */
							if (he && off1 == he->item_offset) {
								crc = *((uint64_t *)sim_ck);
								he = NULL;
							}
						}
						if (he) {
							/*
							 * Match found. Load segment metadata from disk and perform
							 * identity deduplication with the segment chunks.
							 */
							crc = *((uint64_t *)sim_ck);
							offset = he->item_offset;
							off1 = offset;
							if (db_segcache_map(cfg, ctx->id, &o_blks, &offset,
							    (uchar_t **)&seg_blocks) == -1) {
								fprintf(stderr, "Segment cache mmap failed.\n");
								ctx->valid = 0;
								return (0);
							}

							/*
							 * Now lookup loaded segment blocks in hashtable. If match is
							 * found then the hashtable entry is updated to point to the
							 * loaded segment block.
							 */
							for (k=0; k<o_blks; k++) {
								uint32_t hent;
								hent = XXH32(seg_blocks[k].cksum, cfg->chunk_cksum_sz, 0);
								hent ^= (hent / cfg->chunk_cksum_sz);
								hent = hent % cfg->segment_sz;

								if (htab[hent] != NULL) {
									be = htab[hent];
									do {
										if (be->length & RABIN_INDEX_FLAG)
											goto next_ent;
										if (ckcmp(seg_blocks[k].cksum,
										    be->cksum, cfg->chunk_cksum_sz) == 0 &&
										    seg_blocks[k].length == be->length) {
											be->length = (be->length |
												RABIN_INDEX_FLAG) &
												CLEAR_SIMILARITY_FLAG;
											be->offset = seg_blocks[k].offset +
												offset;
											break;
										}
next_ent:
										if (be->next)
											be = be->next;
										else
											break;
									} while(1);
								}
							}
						}
						sim_ck -= cfg->similarity_cksum_sz;
					}
					i = blks;
				}

				/*
				 * Signal the next thread in sequence to access the index.
				 */
				sem_post(ctx->index_sem_next);

				/*======================================================================
				 * Finally scan the blocks array and update dedupe index.
				 *======================================================================
				 */
				length = 0;
				for (i=0; i<blknum; i++) {

					if (!(ctx->g_blocks[i].length & RABIN_INDEX_FLAG)) {
						/*
						 * Block match in index was not found.
						 * Block was added to index. Merge this block.
						 */
						if (length + ctx->g_blocks[i].length > RABIN_MAX_BLOCK_SIZE) {
							*((uint32_t *)g_dedupe_idx) = LE32(length);
							g_dedupe_idx += RABIN_ENTRY_SIZE;
							length = 0;
							dedupe_index_sz++;
						}
						length += ctx->g_blocks[i].length;
					} else {
						/*
						 * Block match in index was found.
						 */
						if (length > 0) {
							/*
							 * Write pending accumulated block length value.
							 */
							*((uint32_t *)g_dedupe_idx) = LE32(length);
							g_dedupe_idx += RABIN_ENTRY_SIZE;
							length = 0;
							dedupe_index_sz++;
						}
						/*
						 * Add a reference entry to the dedupe array.
						 */
						*((uint32_t *)g_dedupe_idx) = LE32(ctx->g_blocks[i].length);
						g_dedupe_idx += RABIN_ENTRY_SIZE;
						*((uint64_t *)g_dedupe_idx) = LE64(ctx->g_blocks[i].offset);
						g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);
						matchlen += (ctx->g_blocks[i].length & RABIN_INDEX_VALUE);
						dedupe_index_sz += 3;
					}
				}

				/*
				 * Write final pending block length value (if any).
				 */
				if (length > 0) {
					*((uint32_t *)g_dedupe_idx) = LE32(length);
					g_dedupe_idx += RABIN_ENTRY_SIZE;
					length = 0;
					dedupe_index_sz++;
				}

				blknum = dedupe_index_sz; // Number of entries in block list
				tgt = g_dedupe_idx;
				g_dedupe_idx = ctx->cbuf + RABIN_HDR_SIZE;
				dedupe_index_sz = tgt - g_dedupe_idx;
				src = buf1;
				g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);
			}

			/*
			 * Deduplication reduction should at least be greater than block list metadata.
			 */
			if (matchlen < dedupe_index_sz) {
				DEBUG_STAT_EN(en = get_wtime_millis());
				DEBUG_STAT_EN(fprintf(stderr, "Chunking speed %.3f MB/s, Overall Dedupe speed %.3f MB/s\n", 
					get_mb_s(*size, strt, en_1), get_mb_s(*size, strt, en - (w2 - w1))));
				DEBUG_STAT_EN(fprintf(stderr, "No Dedupe possible."));
				ctx->valid = 0;
				return (0);
			}

			/*
			 * Now copy the block data;
			 */
			for (i=0; i<blknum-2;) {
				length = LE32(*((uint32_t *)g_dedupe_idx));
				g_dedupe_idx += RABIN_ENTRY_SIZE;
				++i;

				j = length & RABIN_INDEX_FLAG;
				length = length & RABIN_INDEX_VALUE;
				if (!j) {
					memcpy(tgt, src, length);
					tgt += length;
					src += length;
				} else {
					src += length;
					g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);
					i += 2;
				}
			}
			pos1 = tgt - ctx->cbuf;
			blknum |= GLOBAL_FLAG;
			goto dedupe_done;
		}

		/*
		 * Subsequent processing below is for per-segment Deduplication.
		 */

		/*
		 * Compute hash signature for each block. We do this in a separate loop to 
		 * have a fast linear scan through the buffer.
		 */
		if (ctx->delta_flag) {
#if defined(_OPENMP)
#	pragma omp parallel for if (mt)
#endif
			for (i=0; i<blknum; i++) {
				ctx->blocks[i]->hash = XXH32(buf1+ctx->blocks[i]->offset,
								ctx->blocks[i]->length, 0);
			}
		} else {
#if defined(_OPENMP)
#	pragma omp parallel for if (mt)
#endif
			for (i=0; i<blknum; i++) {
				ctx->blocks[i]->hash = XXH32(buf1+ctx->blocks[i]->offset,
								ctx->blocks[i]->length, 0);
				ctx->blocks[i]->similarity_hash = ctx->blocks[i]->hash;
			}
		}

		ary_sz = (blknum << 1) * sizeof (rabin_blockentry_t *);
		htab = (rabin_blockentry_t **)(ctx->cbuf + ctx->real_chunksize - ary_sz);
		memset(htab, 0, ary_sz);

		/*
		 * Perform hash-matching of blocks and use a bucket-chained hashtable to match
		 * for duplicates and similar blocks. Unique blocks are inserted and duplicates
		 * and similar ones are marked in the block array.
		 *
		 * Hashtable memory is not allocated. We just use available space in the
		 * target buffer.
		 */
		matchlen = 0;
		for (i=0; i<blknum; i++) {
			uint64_t ck;

			/*
			 * Bias hash with length for fewer collisions. If Delta Compression is
			 * not enabled then value of similarity_hash == hash.
			 */
			ck = ctx->blocks[i]->similarity_hash;
			ck ^= (ck / ctx->blocks[i]->length);
			j = ck % (blknum << 1);

			if (htab[j] == 0) {
				/*
				 * Hash bucket empty. So add block into table.
				 */
				htab[j] = ctx->blocks[i];
				ctx->blocks[i]->other = 0;
				ctx->blocks[i]->next = 0;
				ctx->blocks[i]->similar = 0;
			} else {
				be = htab[j];
				length = 0;

				/*
				 * Look for exact duplicates. Same cksum, length and memcmp()
				 */
				while (1) {
					if (be->hash == ctx->blocks[i]->hash &&
					    be->length == ctx->blocks[i]->length &&
					    memcmp(buf1 + be->offset, buf1 + ctx->blocks[i]->offset,
					    be->length) == 0) {
						ctx->blocks[i]->similar = SIMILAR_EXACT;
						ctx->blocks[i]->other = be;
						be->similar = SIMILAR_REF;
						matchlen += be->length;
						length = 1;
						break;
					}
					if (be->next)
						be = be->next;
					else
						break;
				}

				if (ctx->delta_flag && !length) {
					/*
					 * Look for similar blocks.
					 */
					be = htab[j];
					while (1) {
						if (be->similarity_hash == ctx->blocks[i]->similarity_hash &&
						    be->length == ctx->blocks[i]->length) {
							uint64_t off_diff;
							if (be->offset > ctx->blocks[i]->offset)
								off_diff = be->offset - ctx->blocks[i]->offset;
							else
								off_diff = ctx->blocks[i]->offset - be->offset;

							if (off_diff > ctx->deltac_min_distance) {
								ctx->blocks[i]->similar = SIMILAR_PARTIAL;
								ctx->blocks[i]->other = be;
								be->similar = SIMILAR_REF;
								matchlen += (be->length>>1);
								length = 1;
								break;
							}
						}
						if (be->next)
							be = be->next;
						else
							break;
					}
				}
				/*
				 * No duplicate in table for this block. So add it to
				 * the bucket chain.
				 */
				if (!length) {
					ctx->blocks[i]->other = 0;
					ctx->blocks[i]->next = 0;
					ctx->blocks[i]->similar = 0;
					be->next = ctx->blocks[i];
					DEBUG_STAT_EN(++hash_collisions);
				}
			}
		}
		DEBUG_STAT_EN(fprintf(stderr, "Total Hashtable bucket collisions: %u\n", hash_collisions));

		dedupe_index_sz = (uint64_t)blknum * RABIN_ENTRY_SIZE;
		if (matchlen < dedupe_index_sz) {
			DEBUG_STAT_EN(en = get_wtime_millis());
			DEBUG_STAT_EN(fprintf(stderr, "Chunking speed %.3f MB/s, Overall Dedupe speed %.3f MB/s\n",
					      get_mb_s(*size, strt, en_1), get_mb_s(*size, strt, en)));
			DEBUG_STAT_EN(fprintf(stderr, "No Dedupe possible.\n"));
			ctx->valid = 0;
			return (0);
		}

		dedupe_index = (uint32_t *)(ctx->cbuf + RABIN_HDR_SIZE);
		pos = 0;
		DEBUG_STAT_EN(merge_count = 0);

		/*
		 * Merge runs of unique blocks into a single block entry to reduce
		 * dedupe index size.
		 */
		for (i=0; i<blknum;) {
			dedupe_index[pos] = i;
			ctx->blocks[i]->index = pos;
			++pos;
			length = 0;
			j = i;
			if (ctx->blocks[i]->similar == 0) {
				while (i< blknum && ctx->blocks[i]->similar == 0 &&
				   length < RABIN_MAX_BLOCK_SIZE) {
					length += ctx->blocks[i]->length;
					++i;
					DEBUG_STAT_EN(++merge_count);
				}
				ctx->blocks[j]->length = length;
			} else {
				++i;
			}
		}
		DEBUG_STAT_EN(fprintf(stderr, "Merge count: %u\n", merge_count));

		/*
		 * Final pass update dedupe index and copy data.
		 */
		blknum = pos;
		dedupe_index_sz = (uint64_t)blknum * RABIN_ENTRY_SIZE;
		pos1 = dedupe_index_sz + RABIN_HDR_SIZE;
		matchlen = ctx->real_chunksize - *size;
		for (i=0; i<blknum; i++) {
			be = ctx->blocks[dedupe_index[i]];
			if (be->similar == 0 || be->similar == SIMILAR_REF) {
				/* Just copy. */
				dedupe_index[i] = htonl(be->length);
				memcpy(ctx->cbuf + pos1, buf1 + be->offset, be->length);
				pos1 += be->length;
			} else {
				if (be->similar == SIMILAR_EXACT) {
					dedupe_index[i] = htonl((be->other->index | RABIN_INDEX_FLAG) &
					    CLEAR_SIMILARITY_FLAG);
				} else {
					uchar_t *oldbuf, *newbuf;
					int32_t bsz;
					/*
					 * Perform bsdiff.
					 */
					oldbuf = buf1 + be->other->offset;
					newbuf = buf1 + be->offset;
					DEBUG_STAT_EN(++delta_calls);

					bsz = bsdiff(oldbuf, be->other->length, newbuf, be->length,
					    ctx->cbuf + pos1, buf1 + *size, matchlen);
					if (bsz == 0) {
						DEBUG_STAT_EN(++delta_fails);
						memcpy(ctx->cbuf + pos1, newbuf, be->length);
						dedupe_index[i] = htonl(be->length);
						pos1 += be->length;
					} else {
						dedupe_index[i] = htonl(be->other->index |
						    RABIN_INDEX_FLAG | SET_SIMILARITY_FLAG);
						pos1 += bsz;
					}
				}
			}
		}

dedupe_done:
		if (valid) {
			uchar_t *cbuf = ctx->cbuf;
			uint64_t *entries;
			DEBUG_STAT_EN(uint64_t sz);
			DEBUG_STAT_EN(sz = *size);
			*((uint32_t *)cbuf) = htonl(blknum);
			cbuf += sizeof (uint32_t);
			entries = (uint64_t *)cbuf;
			entries[0] = htonll(*size);
			entries[1] = 0;
			entries[2] = htonll(pos1 - dedupe_index_sz - RABIN_HDR_SIZE);
			*size = pos1;
			ctx->valid = 1;
			DEBUG_STAT_EN(en = get_wtime_millis());
			DEBUG_STAT_EN(fprintf(stderr, "Deduped size: %" PRId64 ", blknum: %u, delta_calls: %u, delta_fails: %u\n",
					     *size, (unsigned int)(blknum & CLEAR_GLOBAL_FLAG), delta_calls, delta_fails));
			DEBUG_STAT_EN(fprintf(stderr, "Chunking speed %.3f MB/s, Overall Dedupe speed %.3f MB/s\n",
					      get_mb_s(sz, strt, en_1), get_mb_s(sz, strt, en)));
			/*
			 * Remaining header entries: size of compressed index and size of
			 * compressed data are inserted later via rabin_update_hdr, after actual compression!
			 */
			return (dedupe_index_sz);
		}
	}
	return (0);
}

void
update_dedupe_hdr(uchar_t *buf, uint64_t dedupe_index_sz_cmp, uint64_t dedupe_data_sz_cmp)
{
	uint64_t *entries;

	buf += sizeof (uint32_t);
	entries = (uint64_t *)buf;
	entries[1] = htonll(dedupe_index_sz_cmp);
	entries[3] = htonll(dedupe_data_sz_cmp);
}

void
parse_dedupe_hdr(uchar_t *buf, uint32_t *blknum, uint64_t *dedupe_index_sz,
		uint64_t *dedupe_data_sz, uint64_t *dedupe_index_sz_cmp,
		uint64_t *dedupe_data_sz_cmp, uint64_t *deduped_size)
{
	uint64_t *entries;

	*blknum = ntohl(*((uint32_t *)(buf)));
	buf += sizeof (uint32_t);

	entries = (uint64_t *)buf;
	*dedupe_data_sz = ntohll(entries[0]);
	*dedupe_index_sz = (uint64_t)(*blknum & CLEAR_GLOBAL_FLAG) * RABIN_ENTRY_SIZE;
	*dedupe_index_sz_cmp =  ntohll(entries[1]);
	*deduped_size = ntohll(entries[2]);
	*dedupe_data_sz_cmp = ntohll(entries[3]);
}

void
dedupe_decompress(dedupe_context_t *ctx, uchar_t *buf, uint64_t *size)
{
	uint32_t blknum, blk, oblk, len;
	uint32_t *dedupe_index;
	uint64_t data_sz, sz, indx_cmp, data_sz_cmp, deduped_sz;
	uint64_t dedupe_index_sz, pos1;
	uchar_t *pos2;

	parse_dedupe_hdr(buf, &blknum, &dedupe_index_sz, &data_sz, &indx_cmp, &data_sz_cmp, &deduped_sz);
	dedupe_index = (uint32_t *)(buf + RABIN_HDR_SIZE);
	pos1 = dedupe_index_sz + RABIN_HDR_SIZE;
	pos2 = ctx->cbuf;
	sz = 0;
	ctx->valid = 1;

	/*
	 * Handling for Global Deduplication.
	 */
	if (blknum & GLOBAL_FLAG) {
		uchar_t *g_dedupe_idx, *src1, *src2;
		uint64_t adj, offset;
		uint32_t flag;

		blknum &= CLEAR_GLOBAL_FLAG;
		g_dedupe_idx = buf + RABIN_HDR_SIZE;
		offset = LE64(*((uint64_t *)g_dedupe_idx));
		g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);
		blknum -= 2;
		src1 = buf + RABIN_HDR_SIZE + dedupe_index_sz;

		sem_wait(ctx->index_sem);
		for (blk=0; blk<blknum;) {
			len = LE32(*((uint32_t *)g_dedupe_idx));
			g_dedupe_idx += RABIN_ENTRY_SIZE;
			++blk;
			flag = len & RABIN_INDEX_FLAG;
			len &= RABIN_INDEX_VALUE;

			if (sz + len > data_sz) {
				fprintf(stderr, "Dedup data overflows chunk.\n");
				ctx->valid = 0;
				break;
			}
			if (flag == 0) {
				memcpy(pos2, src1, len);
				pos2 += len;
				src1 += len;
				sz += len;
			} else {
				pos1 = LE64(*((uint64_t *)g_dedupe_idx));
				g_dedupe_idx += (RABIN_ENTRY_SIZE * 2);
				blk += 2;

				/*
				 * Handling of chunk references at duplicate chunks.
				 * 
				 * If required data offset is greater than the current segment's starting
				 * offset then the referenced chunk is already in the current segment in
				 * RAM. Just mem-copy it.
				 * Otherwise it will be in the current output file. We mmap() the relevant
				 * region and copy it. The way deduplication is done it is guaranteed that
				 * all duplicate references will be backward references so this approach works.
				 * 
				 * However this approach precludes pipe-mode streamed decompression since
				 * it requires random access to the output file.
				 */
				if (pos1 > offset) {
					src2 = ctx->cbuf + (pos1 - offset);
					memcpy(pos2, src2, len);
				} else {
					adj = pos1 % ctx->pagesize;
					src2 = mmap(NULL, len + adj, PROT_READ, MAP_SHARED, ctx->out_fd, pos1 - adj);
					if (src2 == NULL) {
						perror("MMAP failed ");
						ctx->valid = 0;
						break;
					}
					memcpy(pos2, src2 + adj, len);
					munmap(src2, len + adj);
				}
				pos2 += len;
				sz += len;
			}
		}
		*size = data_sz;
		return;
	}

	/*
	 * Handling for per-segment Deduplication.
	 * First pass re-create the rabin block array from the index metadata.
	 * Second pass copy over blocks to the target buffer to re-create the original segment.
	 */
	slab_cache_add(sizeof (rabin_blockentry_t));
	for (blk = 0; blk < blknum; blk++) {
		if (ctx->blocks[blk] == 0)
			ctx->blocks[blk] = (rabin_blockentry_t *)slab_alloc(NULL, sizeof (rabin_blockentry_t));
		len = ntohl(dedupe_index[blk]);
		ctx->blocks[blk]->hash = 0;
		if (len == 0) {
			ctx->blocks[blk]->hash = 1;

		} else if (!(len & RABIN_INDEX_FLAG)) {
			ctx->blocks[blk]->length = len;
			ctx->blocks[blk]->offset = pos1;
			pos1 += len;
		} else {
			bsize_t blen;

			ctx->blocks[blk]->length = 0;
			if (len & GET_SIMILARITY_FLAG) {
				ctx->blocks[blk]->offset = pos1;
				ctx->blocks[blk]->index = (len & RABIN_INDEX_VALUE) | SET_SIMILARITY_FLAG;
				blen = get_bsdiff_sz(buf + pos1);
				pos1 += blen;
			} else {
				ctx->blocks[blk]->index = len & RABIN_INDEX_VALUE;
			}
		}
	}

	for (blk = 0; blk < blknum; blk++) {
		int rv;
		bsize_t newsz;

		if (ctx->blocks[blk]->hash == 1) continue;
		if (ctx->blocks[blk]->length > 0) {
			len = ctx->blocks[blk]->length;
			pos1 = ctx->blocks[blk]->offset;
		} else {
			oblk = ctx->blocks[blk]->index;

			if (oblk & GET_SIMILARITY_FLAG) {
				oblk = oblk & CLEAR_SIMILARITY_FLAG;
				len = ctx->blocks[oblk]->length;
				pos1 = ctx->blocks[oblk]->offset;
				newsz = data_sz - sz;
				rv = bspatch(buf + ctx->blocks[blk]->offset, buf + pos1, len, pos2, &newsz);
				if (rv == 0) {
					fprintf(stderr, "Failed to bspatch block.\n");
					ctx->valid = 0;
					break;
				}
				pos2 += newsz;
				sz += newsz;
				if (sz > data_sz) {
					fprintf(stderr, "Dedup data overflows chunk.\n");
					ctx->valid = 0;
					break;
				}
				continue;
			} else {
				len = ctx->blocks[oblk]->length;
				pos1 = ctx->blocks[oblk]->offset;
			}
		}
		memcpy(pos2, buf + pos1, len);
		pos2 += len;
		sz += len;
		if (sz > data_sz) {
			fprintf(stderr, "Dedup data overflows chunk.\n");
			ctx->valid = 0;
			break;
		}
	}
	if (ctx->valid && sz < data_sz) {
		fprintf(stderr, "Too little dedup data processed.\n");
		ctx->valid = 0;
	}
	*size = data_sz;
}
