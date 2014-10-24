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

#ifndef	_PCOMPRESS_H
#define	_PCOMPRESS_H

#include <pcompress.h>
#include "utils/utils.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	METADATA_CHUNK_SIZE	(2 * 1024 * 1024)
#define	METADATA_HDR_SZ		(CHUNK_HDR_SZ + COMPRESSED_CHUNKSIZE + pctx->mac_bytes)

struct _meta_ctx {
	int meta_pipes[2];
	pc_ctx_t *pctx;
	pthread_t meta_thread;
	uchar_t *frombuf, *tobuf;
	uint64_t frompos;
	uchar_t checksum[CKSUM_MAX_BYTES];
	void *lz4_dat;
	int comp_level;
	mac_ctx_t chunk_hmac;
} meta_ctx_t;

struct _meta_msg {
	uchar_t *buf;
	size_t len;
} meta_msg_t;

meta_ctx_t *meta_ctx_create(pc_ctx_t *pctx, int file_version int comp_fd);
int meta_ctx_write(meta_ctx_t *mctx, meta_msg_t *msg);
int meta_ctx_read(meta_ctx_t *mctx, meta_msg_t *msg);
int meta_ctx_done(meta_ctx_t *mctx);
void meta_ctx_close_sink_channel(meta_ctx_t *mctx);
void meta_ctx_close_src_channel(meta_ctx_t *mctx);

#ifdef	__cplusplus
}
#endif

#endif
