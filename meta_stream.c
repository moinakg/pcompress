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
#include "utils/utils.h"
#include "allocator.h"
#include "meta_stream.h"

extern int lz4_compress(void *src, uint64_t srclen, void *dst, uint64_t *dstlen,
               int level, uchar_t chdr, int btype, void *data);

enum {
	SRC_CHANNEL = 0,
	SINK_CHANNEL
};

/*
 * Metadata chunk header format:
 * 64-bit integer = 1: Compressed length: This indicates that this is a metadata chunk
 * 64-bit integer: Real Compressed length
 * 64-bit integer: Uncompressed original length
 * 1 Byte: Chunk flag
 * Upto 64-bytes: Checksum, HMAC if encrypting
 * 32-bit integer: Header CRC32 if not encrypting
 */
static int
compress_and_write(meta_ctx_t *mctx)
{
	pc_ctx_t pctx = mctx->pctx;
	uchar_t type;
	uchar_t *comp_chunk, *tobuf;
	int rv;
	uint64_t dstlen;
	int64_t wbytes;

	/*
	 * Plain checksum if not encrypting.
	 * This place will hold HMAC if encrypting.
	 */
	if (!pctx->encrypt_type) {
		compute_checksum(mctx->checksum, pctx->cksum, mctx->frombuf,
		    mctx->frompos, 0, 1);
	}

	type = COMPRESSED;
	U64_P(mctx->tobuf) = LE64(1); // Indicate metadata chunk
	tobuf = mctx->tobuf + 8;
	comp_chunk = mctx->tobuf + METADATA_HDR_SZ;
	dstlen = METADATA_CHUNK_SIZE;

	/*
	 * Ok, now compress.
	 */
	rv = lz4_compress(mctx->frombuf, mctx->frompos, comp_chunk, &dstlen, mctx->level,
	    0, TYPE_BINARY, mctx->lz4_dat);

	if (rv < 0) {
		type = UNCOMPRESSED;
		memcpy(comp_chunk, mctx->frombuf, mctx->frompos);
	}

	if (pctx->encrypt_type) {
		rv = crypto_buf(&(pctx->crypto_ctx), comp_chunk, comp_chunk, dstlen, 255);
		if (rv == -1) {
			pctx->main_cancel = 1;
			pctx->t_errored = 1;
			log_msg(LOG_ERR, 0, "Metadata Encrypion failed")
			return (0);
		}
	}

	/*
	 * Add header size to the compressed length minus the initial 64-bit value.
	 * That one is a flag value which is read separately during decompression.
	 */
	dstlen += METADATA_HDR_SZ - COMPRESSED_CHUNKSIZE;
	U64_P(mctx->tobuf + 8) = LE64(dstlen);
	U64_P(mctx->tobuf + 16) = LE64(mctx->frompos);
	if (!pctx->encrypt_type)
		serialize_checksum(mctx->checksum, mctx->tobuf + 24, pctx->cksum_bytes);

	if (pctx->encrypt_type) {
		uchar_t mac_ptr;
		uchar_t chash[pctx->mac_bytes];
		unsigned int hlen;

		mac_ptr = mctx->tobuf + 24 + pctx->cksum_bytes; // cksum_bytes will be 0 here but ...
		memset(mac_ptr, 0, pctx->mac_bytes);
		hmac_reinit(&mctx->chunk_hmac);
		hmac_update(&tdat->chunk_hmac, tobuf, dstlen);
		hmac_final(&tdat->chunk_hmac, chash, &hlen);
		serialize_checksum(chash, mac_ptr, hlen);
	} else {
		uchar_t mac_ptr;
		uint32_t crc;

		mac_ptr = mctx->tobuf + 24 + pctx->cksum_bytes;
		memset(mac_ptr, 0, pctx->mac_bytes);
		crc = lzma_crc32(tdat->cmp_seg, rbytes, 0);
		U32_P(mac_ptr) = LE32(crc);
	}

	/*
	 * All done. Now grab lock and write.
	 */
	dstlen += COMPRESSED_CHUNKSIZE; // The 'full' chunk now
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
	if (mctx->pipes[SINK_CHANNEL]) {
		close(mctx->pipes[SINK_CHANNEL]);
		mctx->pipes[SINK_CHANNEL] = 0;
	}
}

void
meta_ctx_close_src_channel(meta_ctx_t *mctx)
{
	if (mctx->pipes[SRC_CHANNEL]) {
		close(mctx->pipes[SRC_CHANNEL]);
		mctx->pipes[SRC_CHANNEL] = 0;
	}
}

static void *
metadata_compress(void *dat)
{
	meta_ctx_t *mctx = (meta_ctx_t *)dat;
	meta_msg_t msg;
	int ack;

	while (Read(mctx->meta_pipes[SINK_CHANNEL], &msg, sizeof (msg)) == sizeof (msg)) {
		ack = 0;
		if (mctx->frompos + msg.len > METADATA_CHUNK_SIZE) {
			if (!compress_and_write(mctx)) {
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
			mctx->frompos = 0;
			memcpy(mctx->frombuf, msg.buf, msg.len);
			mctx->frompos = msg.len;

		} else if (mctx->frompos + msg.len == METADATA_CHUNK_SIZE) {
			memcpy(mctx->frombuf + mctx->frompos, msg.buf, msg.len);
			if (!compress_and_write(mctx)) {
				Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
				return (NULL);
			}
			mctx->frompos = 0;
		} else {
			memcpy(mctx->frombuf + mctx->frompos, msg.buf, msg.len);
			mctx->frompos += msg.len;
		}
		ack = 1;
		Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack))
	}

	/*
	 * Flush pending chunk.
	 */
	if (mctx->frompos) {
		if (!compress_and_write(mctx)) {
			ack = 0;
			Write(mctx->meta_pipes[SINK_CHANNEL], &ack, sizeof (ack));
			return (NULL);
		}
		mctx->frompos = 0;
	}
}

static void *
metadata_decompress(void *dat)
{
}

meta_ctx_t *
meta_ctx_create(pc_ctx_t *pctx, int file_version, int comp_fd)
{
	meta_ctx_t *mctx;

	mctx = (meta_ctx_t *)malloc(sizeof (meta_ctx_t));
	if (!mctx) {
		log_msg(LOG_ERR, 1, "Failed to allocate metadata context.");
		return (NULL);
	}

	if (pctx->encrypt_type) {
		if (hmac_init(&mctx->chunk_hmac, pctx->cksum, &(pctx->crypto_ctx)) == -1) {
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

	mctx->pctx = pctx;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, mctx->meta_pipes) == -1) {
		(void) free(mctx->frombuf);
		(void) free(mctx->tobuf);
		(void) free(mctx);
		log_msg(LOG_ERR, 1, "Unable to create a metadata ctx channel.");
		return (NULL);
	}

	if (pctx->do_compress) {
		int rv, level;

		mctx->comp_level = pctx->level;
		rv = lz4_init(&mctx->lz4_dat, &mctx->comp_level, 1, METADATA_CHUNK_SIZE,
		    file_version, COMPRESS);
		if (rv != 0 || pthread_create(&(mctx->meta_thread), NULL, metadata_compress,
		    (void *)mctx) != 0) {
			(void) close(pctx->meta_pipes[0]);
			(void) close(pctx->meta_pipes[1]);
			(void) free(mctx->frombuf);
			(void) free(mctx->tobuf);
			(void) free(mctx);
			if (rv == 0)
				log_msg(LOG_ERR, 0, "LZ4 init failed.");
			else
				log_msg(LOG_ERR, 1, "Unable to create metadata thread.");
			return (NULL);
		}
	} else {
		int rv, level;

		mctx->comp_level = pctx->level;
		rv = lz4_init(&mctx->lz4_dat, &mctx->comp_level, 1, METADATA_CHUNK_SIZE,
		    file_version, DECOMPRESS);
		if (rv != 0 || pthread_create(&(mctx->meta_thread), NULL, metadata_decompress,
		    (void *)mctx) != 0) {
			(void) close(pctx->meta_pipes[0]);
			(void) close(pctx->meta_pipes[1]);
			(void) free(mctx->frombuf);
			(void) free(mctx->tobuf);
			(void) free(mctx);
			if (rv == 0)
				log_msg(LOG_ERR, 0, "LZ4 init failed.");
			else
				log_msg(LOG_ERR, 1, "Unable to create metadata thread.");
			return (NULL);
		}
	}

	return (mctx);
}


int
meta_ctx_write(meta_ctx_t *mctx, meta_msg_t *msg)
{
	int ack;
	meta_msg_t msg;

	/*
	 * Write the message buffer to the pipe.
	 */
	if (Write(pctx->meta_pipes[SRC_CHANNEL], &msg, sizeof (msg)) < sizeof (meta_msg_t)) {
		log_msg(LOG_ERR, 1, "Meta socket write error.");
		return (0);
	}

	/*
	 * Wait for ACK.
	 */
	if (Read(pctx->meta_pipes[SRC_CHANNEL], &ack, sizeof (ack)) < sizeof (ack)) {
		log_msg(LOG_ERR, 1, "Meta socket read error.");
		return (0);
	}

	if (!ack) {
		return (-1);
	}

	return (1);
}

int
meta_ctx_read(meta_ctx_t *mctx, meta_msg_t *msg)
{
}

int
meta_ctx_done(meta_ctx_t *mctx)
{
}

