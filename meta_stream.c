/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2014 Moinak Ghosh. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include "pcompress.h"
#include "filters/delta2/delta2.h"
#include "utils/utils.h"
#include "lzma/lzma_crc.h"
#include "allocator.h"
#include "meta_stream.h"

#define	METADATA_CHUNK_SIZE	(3 * 1024 * 1024)

extern int bzip2_compress(void *src, uint64_t srclen, void *dst, uint64_t *dstlen,
               int level, uchar_t chdr, int btype, void *data);
extern int bzip2_decompress(void *src, uint64_t srclen, void *dst, uint64_t *dstlen,
		 int level, uchar_t chdr, int btype, void *data);
extern void bzip2_props(algo_props_t *data, int level, uint64_t chunksize);

enum {
	SRC_CHANNEL = 0,
	SINK_CHANNEL
};

struct _meta_ctx {
	int meta_pipes[2];
	pc_ctx_t *pctx;
	pthread_t meta_thread;
	uchar_t *frombuf, *tobuf;
	uint64_t frompos, topos, tosize;
	uchar_t checksum[CKSUM_MAX_BYTES];
	void *bzip2_dat;
	int comp_level, id;
	int comp_fd;
	int running;
	int delta2_nstrides;
	int do_compress;
	mac_ctx_t chunk_hmac;
	algo_props_t props;
};

static int
compress_and_write(meta_ctx_t *mctx)
{
	pc_ctx_t *pctx = mctx->pctx;
	uchar_t type;
	uchar_t *comp_chunk, *tobuf;
	int rv;
	uint64_t dstlen;
	int64_t wbytes;

	/*
	 * Increment metadata chunk id. Useful when encrypting (CTR Mode).
	 */
	mctx->id++;

	/*
	 * Plain checksum if not encrypting.
	 * This place will hold HMAC if encrypting.
	 */
	if (!pctx->encrypt_type) {
		compute_checksum(mctx->checksum, pctx->cksum, mctx->frombuf,
		    mctx->frompos, 0, 1);
	}

	type = 0;
	/*
	 * This is normally the compressed chunk size for data chunks. Here we
	 * set it to 1 to indicate that this is a metadata chunk. This value is
	 * always big-endian format. The next value is the real compressed
	 * chunk size.
	 */
	tobuf = mctx->tobuf;
	U64_P(tobuf) = htonll(METADATA_INDICATOR);
	U64_P(tobuf + 16) = LE64(mctx->frompos); // Record original length
	comp_chunk = tobuf + METADATA_HDR_SZ;
	dstlen = mctx->frompos;

	/*
	 * Apply Delta2 filter.
	 */
	rv = delta2_encode(mctx->frombuf, mctx->frompos, comp_chunk, &dstlen,
	    mctx->props.delta2_span, mctx->delta2_nstrides);
	if (rv != -1) {
		memcpy(mctx->frombuf, comp_chunk, dstlen);
		mctx->frompos = dstlen;
		type |= PREPROC_TYPE_DELTA2;
	} else {
		dstlen = mctx->frompos;
	}

	/*
	 * Ok, now compress.
	 */
	rv = bzip2_compress(mctx->frombuf, mctx->frompos, comp_chunk, &dstlen, mctx->comp_level,
	    0, TYPE_BINARY, mctx->bzip2_dat);

	if (rv < 0) {
		dstlen = mctx->frompos;
		memcpy(comp_chunk, mctx->frombuf, dstlen);
	} else {
		type |= PREPROC_COMPRESSED;
	}

	if (pctx->encrypt_type) {
		rv = crypto_buf(&(pctx->crypto_ctx), comp_chunk, comp_chunk, dstlen, mctx->id);
		if (rv == -1) {
			pctx->main_cancel = 1;
			pctx->t_errored = 1;
			log_msg(LOG_ERR, 0, "Metadata Encrypion failed");
			return (0);
		}
	}

	/*
	 * Store the compressed length of the data segment. While reading we have to account
	 * for the header.
	 */
	U64_P(tobuf + 8) = LE64(dstlen);
	*(tobuf + 24) = type;

	if (!pctx->encrypt_type)
		serialize_checksum(mctx->checksum, tobuf + 25, pctx->cksum_bytes);

	if (pctx->encrypt_type) {
		uchar_t chash[pctx->mac_bytes];
		unsigned int hlen;
		uchar_t *mac_ptr;

		mac_ptr = tobuf + 25;
		memset(mac_ptr, 0, pctx->mac_bytes + CRC32_SIZE);
		hmac_reinit(&mctx->chunk_hmac);
		hmac_update(&mctx->chunk_hmac, tobuf, dstlen + METADATA_HDR_SZ);
		hmac_final(&mctx->chunk_hmac, chash, &hlen);
		serialize_checksum(chash, mac_ptr, hlen);
	} else {
		uint32_t crc;
		uchar_t *mac_ptr;

		mac_ptr = tobuf + 25 + CKSUM_MAX;
		memset(mac_ptr, 0, CRC32_SIZE);
		crc = lzma_crc32(tobuf, METADATA_HDR_SZ, 0);
		U32_P(tobuf + 25 + CKSUM_MAX) = LE32(crc);
	}

	/*
	 * All done. Now grab lock and write.
	 */
	dstlen += METADATA_HDR_SZ; // The 'full' chunk now
	pthread_mutex_lock(&pctx->write_mutex);
	wbytes = Write(mctx->comp_fd, mctx->tobuf, dstlen);
	pthread_mutex_unlock(&pctx->write_mutex);
	if (wbytes != dstlen) {
		log_msg(LOG_ERR, 1, "Metadata Write (expected: %" PRIu64 ", written: %" PRId64 ") : ",
			dstlen, wbytes);
		pctx->main_cancel = 1;
		pctx->t_errored = 1;
		return (0);
	}

	return (1);
}

void
meta_ctx_close_sink_channel(meta_ctx_t *mctx)
{
	if (mctx->meta_pipes[SINK_CHANNEL]) {
		close(mctx->meta_pipes[SINK_CHANNEL]);
		mctx->meta_pipes[SINK_CHANNEL] = 0;
	}
}

void
meta_ctx_close_src_channel(meta_ctx_t *mctx)
{
	if (mctx->meta_pipes[SRC_CHANNEL]) {
		close(mctx->meta_pipes[SRC_CHANNEL]);
		mctx->meta_pipes[SRC_CHANNEL] = 0;
	}
}

/*
 * Accumulate metadata into a memory buffer. Once the buffer gets filled or
 * data stream ends, the buffer is compressed and written out.
 */
static void *
metadata_compress(void *dat)
{
	meta_ctx_t *mctx = (meta_ctx_t *)dat;
	meta_msg_t *msgp;
	int ack;

	mctx->running = 1;
	mctx->id = -1;
	while (Read(mctx->meta_pipes[SINK_CHANNEL], &msgp, sizeof (msgp)) == sizeof (msgp)) {
		ack = 0;
		if (mctx->frompos + msgp->len > METADATA_CHUNK_SIZE) {
			/*
			 * Accumulating the metadata block will overflow buffer. Compress
			 * and write the current buffer and then copy the new data into it.
			 */
			if (!compress_and_write(mctx)) {
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
			mctx->frompos = 0;
			memcpy(mctx->frombuf, msgp->buf, msgp->len);
			mctx->frompos = msgp->len;

		} else if (mctx->frompos + msgp->len == METADATA_CHUNK_SIZE) {
			/*
			 * Accumulating the metadata block fills the buffer. Fill it then
			 * compress and write the buffer.
			 */
			memcpy(mctx->frombuf + mctx->frompos, msgp->buf, msgp->len);
			mctx->frompos += msgp->len;
			if (!compress_and_write(mctx)) {
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
			mctx->frompos = 0;
		} else {
			/*
			 * Accumulate the metadata block into the buffer for future
			 * compression.
			 */
			memcpy(mctx->frombuf + mctx->frompos, msgp->buf, msgp->len);
			mctx->frompos += msgp->len;
		}
		ack = 1;
		Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
	}
	mctx->running = 0;

	/*
	 * Flush any accumulated data in the buffer.
	 */
	if (mctx->frompos) {
		if (!compress_and_write(mctx)) {
			ack = 0;
			Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
			return (NULL);
		}
		mctx->frompos = 0;
	}
	return (NULL);
}

static int
decompress_data(meta_ctx_t *mctx)
{
	uint64_t origlen, len_cmp, dstlen;
	uchar_t *cbuf, *cseg, *ubuf, type;
	pc_ctx_t *pctx = mctx->pctx;
	uchar_t checksum[CKSUM_MAX_BYTES];
	int rv;

	cbuf = mctx->frombuf;
	ubuf = mctx->tobuf;
	len_cmp = LE64(U64_P(cbuf + 8));
	origlen = LE64(U64_P(cbuf + 16));
	type = *(cbuf + 24);
	cseg = cbuf + METADATA_HDR_SZ;
	dstlen = origlen;

	/*
	 * If this was encrypted:
	 * Verify HMAC first before anything else and then decrypt compressed data.
	 */
	if (pctx->encrypt_type) {
		unsigned int len;

		len = pctx->mac_bytes;
		deserialize_checksum(checksum, cbuf + 25, pctx->mac_bytes);
		memset(cbuf + 25, 0, pctx->mac_bytes + CRC32_SIZE);
		hmac_reinit(&mctx->chunk_hmac);
		hmac_update(&mctx->chunk_hmac, cbuf, len_cmp + METADATA_HDR_SZ);
		hmac_final(&mctx->chunk_hmac, mctx->checksum, &len);
		if (memcmp(checksum, mctx->checksum, len) != 0) {
			log_msg(LOG_ERR, 0, "Metadata chunk %d, HMAC verification failed",
			    mctx->id);
			return (0);
		}
		rv = crypto_buf(&(pctx->crypto_ctx), cseg, cseg, len_cmp, mctx->id);
		if (rv == -1) {
			/*
			 * Decryption failure is fatal.
			 */
			log_msg(LOG_ERR, 0, "Metadata chunk %d, Decryption failed",
			    mctx->id);
			return (0);
		}
	} else {
		uint32_t crc1, crc2;

		/*
		 * Verify Header CRC32 in non-crypto mode.
		 */
		deserialize_checksum(checksum, cbuf + 25, pctx->cksum_bytes);
		crc1 = U32_P(cbuf + 25 + CKSUM_MAX);
		memset(cbuf + 25 + CKSUM_MAX, 0, CRC32_SIZE);
		crc2 = lzma_crc32(cbuf, METADATA_HDR_SZ, 0);

		if (crc1 != crc2) {
			/*
			 * Header CRC32 verification failure is fatal.
			 */
			log_msg(LOG_ERR, 0, "Metadata chunk %d, Header CRC verification failed",
			    mctx->id);
			return (0);
		}
	}

	if (type & PREPROC_COMPRESSED) {
		rv = bzip2_decompress(cseg, len_cmp, ubuf, &dstlen, mctx->comp_level,
		    0, TYPE_BINARY, mctx->bzip2_dat);
		if (rv == -1) {
			log_msg(LOG_ERR, 0, "Metadata chunk %d, decompression failed.", mctx->id);
			return (0);
		}
	} else {
		memcpy(ubuf, cseg, len_cmp);
	}

	if (type & PREPROC_TYPE_DELTA2) {
		uint64_t _dstlen = origlen;
		rv = delta2_decode(ubuf, dstlen, cseg, &_dstlen);
		if (rv == -1) {
			log_msg(LOG_ERR, 0, "Metadata chunk %d, Delta2 decoding failed.", mctx->id);
			return (0);
		}
		memcpy(ubuf, cseg, _dstlen);
                dstlen = _dstlen;
	}

	/*
	 * Now verify normal checksum if not using encryption.
	 */
	if (!pctx->encrypt_type) {
		compute_checksum(mctx->checksum, pctx->cksum, ubuf, dstlen, 0, 1);
		if (memcmp(checksum, mctx->checksum, pctx->cksum_bytes) != 0) {
			log_msg(LOG_ERR, 0, "Metadata chunk %d, Checksum verification failed",
				mctx->id);
			return (0);
		}
	}
	mctx->topos = 0;
	mctx->tosize = dstlen;
	return (1);
}

static void *
metadata_decompress(void *dat)
{
	meta_ctx_t *mctx = (meta_ctx_t *)dat;
	pc_ctx_t *pctx;
	meta_msg_t *msgp;
	int ack;

	pctx = mctx->pctx;
	mctx->running = 1;
	mctx->topos = mctx->tosize = 0;
	mctx->id = -1;
	while (Read(mctx->meta_pipes[SINK_CHANNEL], &msgp, sizeof (msgp)) == sizeof (msgp)) {
		int64_t rb;
		uint64_t len_cmp;

		/*
		 * Scan to the next metadata chunk and decompress it, if our in-memory data
		 * is fully consumed or not filled.
		 */
		if (mctx->topos == mctx->tosize) {
			uchar_t *frombuf = mctx->frombuf;

			mctx->id++;
			while ((rb = Read(mctx->comp_fd, &len_cmp, sizeof (len_cmp))
			    == sizeof(len_cmp))) {
				len_cmp = ntohll(len_cmp);
				if (len_cmp != METADATA_INDICATOR) {
					uint64_t skiplen;

					if (len_cmp == 0) {
						/*
						 * We have reached the end of the file.
						 */
						msgp->len = 0;
						ack = 1;
						Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
						return (NULL);
					}
					skiplen = len_cmp + pctx->cksum_bytes + pctx->mac_bytes
					    + CHUNK_FLAG_SZ;
					int64_t cpos = lseek(mctx->comp_fd, skiplen, SEEK_CUR);
					if (cpos == -1) {
						log_msg(LOG_ERR, 1, "Cannot find/seek next metadata block.");
						ack = 0;
						Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
						return (NULL);
					}
				} else {
					break;
				}
			}
			if (rb == -1) {
				log_msg(LOG_ERR, 1, "Failed read from metadata fd: ");
				ack = 0;
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);

			} else if (rb == 0) {
				/*
				 * We have reached the end of the file.
				 */
				msgp->len = 0;
				ack = 1;
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
			U64_P(frombuf) = htonll(len_cmp);
			frombuf += 8;

			/*
			 * We are at the start of a metadata chunk. Read the size.
			 */
			if ((rb = Read(mctx->comp_fd, &len_cmp, sizeof (len_cmp)))
			    != sizeof(len_cmp)) {
				log_msg(LOG_ERR, 1, "Failed to read size from metadata fd: %lld", rb);
				ack = 0;
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
			U64_P(frombuf) = len_cmp;
			frombuf += 8;
			len_cmp = LE64(len_cmp);

			/*
			 * Now read the rest of the chunk. This is rest of the header plus the
			 * data segment.
			 */
			len_cmp = len_cmp + (METADATA_HDR_SZ - (frombuf - mctx->frombuf));
			rb = Read(mctx->comp_fd, frombuf, len_cmp);
			if (rb != len_cmp) {
				log_msg(LOG_ERR, 1, "Failed to read chunk from metadata fd: ");
				ack = 0;
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
			mctx->topos = 0;
			mctx->frompos = len_cmp + METADATA_HDR_SZ;

			/*
			 * Now decompress.
			 */
			if (!decompress_data(mctx)) {
				ack = 0;
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
		}

		msgp->buf = mctx->tobuf;
		msgp->len = mctx->tosize;
		mctx->topos = mctx->tosize;
		ack = 1;
		Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
	}

	return (NULL);
}

/*
 * Create the metadata thread and associated buffers. This writes out compressed
 * metadata chunks into the archive. This is libarchive metadata.
 */
meta_ctx_t *
meta_ctx_create(void *pc, int file_version, int comp_fd)
{
	pc_ctx_t *pctx = (pc_ctx_t *)pc;
	meta_ctx_t *mctx;

	mctx = (meta_ctx_t *)malloc(sizeof (meta_ctx_t));
	if (!mctx) {
		log_msg(LOG_ERR, 1, "Failed to allocate metadata context.");
		return (NULL);
	}

	mctx->running = 0;
	if (pctx->encrypt_type) {
		if (hmac_init(&mctx->chunk_hmac, pctx->cksum,
		    &(pctx->crypto_ctx)) == -1) {
			(void) free(mctx);
			log_msg(LOG_ERR, 0, "Cannot initialize metadata hmac.");
			return (NULL);
		}
	}

	mctx->comp_fd = comp_fd;
	mctx->frompos = 0;
	mctx->frombuf = slab_alloc(NULL, METADATA_CHUNK_SIZE + METADATA_HDR_SZ);
	if (!mctx->frombuf) {
		(void) free(mctx);
		log_msg(LOG_ERR, 1, "Failed to allocate metadata buffer.");
		return (NULL);
	}

	mctx->tobuf = slab_alloc(NULL, METADATA_CHUNK_SIZE + METADATA_HDR_SZ);
	if (!mctx->tobuf) {
		(void) free(mctx->frombuf);
		(void) free(mctx);
		log_msg(LOG_ERR, 1, "Failed to allocate metadata buffer.");
		return (NULL);
	}

	/*
	 * The archiver passes metadata via this socketpair. Memory buffer pointers
	 * are passed through the socket for speed rather than the contents.
	 */
	mctx->pctx = pctx;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, mctx->meta_pipes) == -1) {
		(void) free(mctx->frombuf);
		(void) free(mctx->tobuf);
		(void) free(mctx);
		log_msg(LOG_ERR, 1, "Unable to create a metadata ctx channel.");
		return (NULL);
	}

	if (pctx->level > 9)
		mctx->delta2_nstrides = NSTRIDES_EXTRA;
	else
		mctx->delta2_nstrides = NSTRIDES_STANDARD;
	if (pctx->do_compress) {
		int rv;

		mctx->comp_level = pctx->level;
		rv = bzip2_init(&mctx->bzip2_dat, &mctx->comp_level, 1, METADATA_CHUNK_SIZE,
		    file_version, COMPRESS);
		bzip2_props(&mctx->props, pctx->level, METADATA_CHUNK_SIZE);
		if (rv != 0 || pthread_create(&(mctx->meta_thread), NULL, metadata_compress,
		    (void *)mctx) != 0) {
			(void) close(mctx->meta_pipes[0]);
			(void) close(mctx->meta_pipes[1]);
			(void) free(mctx->frombuf);
			(void) free(mctx->tobuf);
			(void) free(mctx);
			if (rv == 0)
				log_msg(LOG_ERR, 0, "Lzma init failed.");
			else
				log_msg(LOG_ERR, 1, "Unable to create metadata thread.");
			return (NULL);
		}
	} else {
		int rv;

		mctx->comp_level = pctx->level;
		rv = bzip2_init(&mctx->bzip2_dat, &mctx->comp_level, 1, METADATA_CHUNK_SIZE,
		    file_version, DECOMPRESS);
		if (rv != 0 || pthread_create(&(mctx->meta_thread), NULL, metadata_decompress,
		    (void *)mctx) != 0) {
			(void) close(mctx->meta_pipes[0]);
			(void) close(mctx->meta_pipes[1]);
			(void) free(mctx->frombuf);
			(void) free(mctx->tobuf);
			(void) free(mctx);
			if (rv == 0)
				log_msg(LOG_ERR, 0, "Lzma init failed.");
			else
				log_msg(LOG_ERR, 1, "Unable to create metadata thread.");
			return (NULL);
		}
	}

	mctx->do_compress = pctx->do_compress;
	return (mctx);
}

int
meta_ctx_send(meta_ctx_t *mctx, const void **buf, size_t *len)
{
	int ack;
	meta_msg_t msg, *msgp;

	/*
	 * Write the message buffer to the pipe.
	 */
	msg.buf = *buf;
	msg.len = *len;
	msgp = &msg;
	if (Write(mctx->meta_pipes[SRC_CHANNEL], &msgp, sizeof (msgp)) <
	    sizeof (msgp)) {
		log_msg(LOG_ERR, 1, "Meta socket write error.");
		return (0);
	}

	/*
	 * Wait for ACK.
	 */
	if (Read(mctx->meta_pipes[SRC_CHANNEL], &ack, sizeof (ack)) <
	    sizeof (ack)) {
		log_msg(LOG_ERR, 1, "Meta socket read error.");
		return (0);
	}

	if (!ack) {
		return (-1);
	}

	*len = msg.len;
	*buf = msg.buf;
	return (1);
}

int
meta_ctx_done(meta_ctx_t *mctx)
{
	meta_ctx_close_src_channel(mctx);
	meta_ctx_close_sink_channel(mctx);
	if (!mctx->do_compress)
		close(mctx->comp_fd);
	pthread_join(mctx->meta_thread, NULL);
	return (0);
}

