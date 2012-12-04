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
 * This program includes partly-modified public domain source
 * code from the LZMA SDK: http://www.7-zip.org/sdk.html
 */

#include <sys/types.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <limits.h>
#include <utils.h>
#include <pcompress.h>
#include <lz4.h>
#include <lz4hc.h>
#include <allocator.h>

#define	LZ4_MAX_CHUNK	2147450621L

struct lz4_params {
	int level;
};

void
lz4_stats(int show)
{
}

int
lz4_buf_extra(ssize_t buflen)
{
	if (buflen > LZ4_MAX_CHUNK)
		buflen = LZ4_MAX_CHUNK;
	return (LZ4_compressBound(buflen) - buflen + sizeof(int));
}

void
lz4_props(algo_props_t *data, int level, ssize_t chunksize) {
	data->compress_mt_capable = 0;
	data->decompress_mt_capable = 0;
	data->buf_extra = lz4_buf_extra(chunksize);
	data->delta2_stride = 50;
}

int
lz4_init(void **data, int *level, int nthreads, ssize_t chunksize,
	 int file_version, compress_op_t op)
{
	struct lz4_params *lzdat;
	int lev;

	if (chunksize > LZ4_MAX_CHUNK) {
		fprintf(stderr, "Max allowed chunk size for LZ4 is: %ld \n",
		    LZ4_MAX_CHUNK);
		return (1);
	}
	lzdat = slab_alloc(NULL, sizeof (struct lz4_params));

	lev = *level;
	if (lev > 3) lev = 3;
	lzdat->level = lev;
	*data = lzdat;

	if (*level > 9) *level = 9;
	return (0);
}

int
lz4_deinit(void **data)
{
	struct lz4_params *lzdat = (struct lz4_params *)(*data);
	
	if (lzdat) {
		slab_free(NULL, lzdat);
	}
	*data = NULL;
	return (0);
}

int
lz4_compress(void *src, size_t srclen, void *dst, size_t *dstlen,
	       int level, uchar_t chdr, void *data)
{
	int rv;
	struct lz4_params *lzdat = (struct lz4_params *)data;
	int _srclen = srclen;
	uchar_t *dst2;

	if (lzdat->level == 1) {
		rv = LZ4_compress(src, dst, _srclen);

	} else if (lzdat->level == 2) {
		rv = LZ4_compress(src, dst, _srclen);
		if (rv == 0 || rv > *dstlen) {
			return (-1);
		}
		dst2 = slab_alloc(NULL, rv + sizeof (int) + LZ4_compressBound(rv));
		*((int *)dst2) = htonl(rv);
		rv = LZ4_compressHC(dst, dst2 + sizeof (int), rv);
		if (rv != 0) {
			rv += sizeof (int);
			memcpy(dst, dst2, rv);
		}
		slab_free(NULL, dst2);
	} else {
		rv = LZ4_compressHC(src, dst, _srclen);
	}
	if (rv == 0) {
		return (-1);
	}
	*dstlen = rv;

	return (0);
}

int
lz4_decompress(void *src, size_t srclen, void *dst, size_t *dstlen,
		 int level, uchar_t chdr, void *data)
{
	int rv;
	struct lz4_params *lzdat = (struct lz4_params *)data;
	int _dstlen = *dstlen;
	uchar_t *dst2;

	if (lzdat->level == 1 || lzdat->level == 3) {
		rv = LZ4_uncompress(src, dst, _dstlen);
		if (rv != srclen) {
			return (-1);
		}

	} else if (lzdat->level == 2) {
		int sz1;

		sz1 = ntohl(*((int *)src));
		rv = LZ4_uncompress(src + sizeof (int), dst, sz1);
		if (rv != srclen - sizeof (int)) {
			return (-1);
		}
		memcpy(src, dst, sz1);
		rv = LZ4_uncompress(src, dst, _dstlen);
		if (rv != sz1) {
			return (-1);
		}
	}
	return (0);
}
