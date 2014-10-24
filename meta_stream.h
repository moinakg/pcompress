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

#ifndef	_META_STREAM_H
#define	_META_STREAM_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The chunk size value which indicates a metadata chunk.
 */
#define METADATA_INDICATOR	1

/*
 * Metadata chunk header format:
 * 64-bit integer = 1: Compressed length: This indicates that this is a metadata chunk
 * 64-bit integer: Compressed length (data portion only)
 * 64-bit integer: Uncompressed original length
 * 1 Byte: Chunk flag
 * Upto 64-bytes: Checksum. This is HMAC if encrypting
 * 32-bit integer: Header CRC32 if not encrypting, otherwise empty.
 */
#define CKSUM_MAX		64
#define CRC32_SIZE		4
#define	METADATA_HDR_SZ		(8 * 3 + 1 + CKSUM_MAX + CRC32_SIZE)

typedef struct _meta_ctx meta_ctx_t;

typedef struct _meta_msg {
	const uchar_t *buf;
	size_t len;
} meta_msg_t;

meta_ctx_t *meta_ctx_create(void *pc, int file_version, int comp_fd);
int meta_ctx_send(meta_ctx_t *mctx, const void **buf, size_t *len);
int meta_ctx_done(meta_ctx_t *mctx);
void meta_ctx_close_sink_channel(meta_ctx_t *mctx);
void meta_ctx_close_src_channel(meta_ctx_t *mctx);

#ifdef	__cplusplus
}
#endif

#endif
