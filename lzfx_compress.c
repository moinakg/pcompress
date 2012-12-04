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
#include <limits.h>
#include <utils.h>
#include <pcompress.h>
#include <lzfx.h>
#include <allocator.h>

struct lzfx_params {
	uint32_t htab_bits;
};

void
lz_fx_stats(int show)
{
}

void
lz_fx_props(algo_props_t *data, int level, ssize_t chunksize) {
	data->delta2_stride = 50;
}

int
lz_fx_init(void **data, int *level, int nthreads, ssize_t chunksize,
	   int file_version, compress_op_t op)
{
	struct lzfx_params *lzdat;
	int lev;

	if (chunksize > UINT_MAX) {
		fprintf(stderr, "Chunk size too big for LZFX.\n");
		return (1);
	}
	lzdat = slab_alloc(NULL, sizeof (struct lzfx_params));

	lev = *level;
	if (lev > 5) lev = 5;
	lzdat->htab_bits = 16 + (lev-1);
	*data = lzdat;

	if (*level > 9) *level = 9;
	return (0);
}

int
lz_fx_deinit(void **data)
{
	struct lzfx_params *lzdat = (struct lzfx_params *)(*data);
	
	if (lzdat) {
		slab_free(NULL, lzdat);
	}
	*data = NULL;
	return (0);
}

static void
lz_fx_err(int err)
{
	switch (err) {
	    case LZFX_ESIZE:
		fprintf(stderr, "LZFX: Output buffer too small.\n");
		break;
	    case LZFX_ECORRUPT:
		fprintf(stderr, "LZFX: Corrupt data for decompression.\n");
		break;
	    case LZFX_EARGS:
		fprintf(stderr, "LZFX: Invalid arguments.\n");
		break;
	    case LZFX_ENOMEM:
		fprintf(stderr, "LZFX: Out of memory when allocating hashtable.\n");
		break;
	    default:
		fprintf(stderr, "LZFX: Unknown error code: %d\n", err);
	}
}

int
lz_fx_compress(void *src, size_t srclen, void *dst, size_t *dstlen,
	       int level, uchar_t chdr, void *data)
{
	int rv;
	struct lzfx_params *lzdat = (struct lzfx_params *)data;
	unsigned int _srclen = srclen;
	unsigned int _dstlen = *dstlen;

	rv = lzfx_compress(src, _srclen, dst, &_dstlen, lzdat->htab_bits);
	if (rv != 0) {
		if (rv != LZFX_ESIZE)
			lz_fx_err(rv);
		return (-1);
	}
	*dstlen = _dstlen;

	return (0);
}

int
lz_fx_decompress(void *src, size_t srclen, void *dst, size_t *dstlen,
		 int level, uchar_t chdr, void *data)
{
	int rv;
	unsigned int _srclen = srclen;
	unsigned int _dstlen = *dstlen;

	rv = lzfx_decompress(src, _srclen, dst, &_dstlen);
	if (rv != 0) {
		lz_fx_err(rv);
		return (-1);
	}
	*dstlen = _dstlen;
	return (0);
}
