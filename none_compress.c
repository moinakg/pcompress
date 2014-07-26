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

#include <sys/types.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <limits.h>
#include <utils.h>
#include <pcompress.h>
#include <lzfx.h>
#include <allocator.h>

void
none_stats(int show)
{
}

int
none_init(void **data, int *level, int nthreads, uint64_t chunksize,
	  int file_version, compress_op_t op)
{
	return (0);
}

void
none_props(algo_props_t *data, int level, uint64_t chunksize) {
	data->compress_mt_capable = 0;
	data->decompress_mt_capable = 0;
	data->buf_extra = 0;
	data->delta2_span = 50;
}

int
none_deinit(void **data)
{
	return (0);
}

int
none_compress(void *src, uint64_t srclen, void *dst, uint64_t *dstlen,
	       int level, uchar_t chdr, int btype, void *data)
{
	memcpy(dst, src, srclen);
	return (0);
}

int
none_decompress(void *src, uint64_t srclen, void *dst, uint64_t *dstlen,
		 int level, uchar_t chdr, int btype, void *data)
{
	memcpy(dst, src, srclen);
	return (0);
}
