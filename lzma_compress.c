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
#include <LzmaEnc.h>
#include <LzmaDec.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

#define	SZ_ERROR_DESTLEN	100
#define	LZMA_DEFAULT_DICT	(1 << 24)

CLzmaEncProps *p = NULL;

static ISzAlloc g_Alloc = {
	slab_alloc,
	slab_free,
	NULL
};

void
lzma_stats(int show)
{
}

/*
 * The two functions below are not thread-safe, by design.
 */
int
lzma_init(void **data, int *level, ssize_t chunksize)
{
	if (!p) {
		p = (CLzmaEncProps *)slab_alloc(NULL, sizeof (CLzmaEncProps));
		LzmaEncProps_Init(p);
		/*
		 * Set the dictionary size and fast bytes based on level.
		 */
		if (*level < 8) {
			/*
			 * Choose a dict size with a balance between perf and
			 * compression.
			 */
			p->dictSize = LZMA_DEFAULT_DICT;
		} else {
			/*
			 * Let LZMA determine best dict size.
			 */
			p->dictSize = 0;
		}
		/* Determine the fast bytes value. */
		if (*level < 7) {
			p->fb = 32;

		} else if (*level < 10) {
			p->fb = 64;

		} else if (*level < 13) {
			p->fb = 64;
			p->mc = 128;

		} else {
			p->fb = 128;
			p->mc = 256;
		}
		p->level = *level;
		LzmaEncProps_Normalize(p);
		slab_cache_add(p->litprob_sz);
	}
	if (*level > 9) *level = 9;
	*data = p;
	return (0);
}

int
lzma_deinit(void **data)
{
	if (p) {
		slab_free(NULL, p);
		p = NULL;
	}
	*data = NULL;
	return (0);
}

static void
lzerr(int err)
{
	switch (err) {
	    case SZ_ERROR_MEM:
		fprintf(stderr, "LZMA: Memory allocation error\n");
		break;
	    case SZ_ERROR_PARAM:
		fprintf(stderr, "LZMA: Incorrect paramater\n");
		break;
	    case SZ_ERROR_WRITE:
		fprintf(stderr, "LZMA: Write callback error\n");
		break;
	    case SZ_ERROR_PROGRESS:
		fprintf(stderr, "LZMA: Progress callback errored\n");
		break;
	    case SZ_ERROR_INPUT_EOF:
		fprintf(stderr, "LZMA: More compressed input bytes expected\n");
		break;
	    case SZ_ERROR_OUTPUT_EOF:
		fprintf(stderr, "LZMA: Output buffer overflow\n");
		break;
	    case SZ_ERROR_UNSUPPORTED:
		fprintf(stderr, "LZMA: Unsupported properties\n");
		break;
	    case SZ_ERROR_DESTLEN:
		fprintf(stderr, "LZMA: Output chunk size too small\n");
		break;
	    case SZ_ERROR_DATA:
		fprintf(stderr, "LZMA: Data Error\n");
		break;
	    default:
		fprintf(stderr, "LZMA: Unknown error code: %d\n", err);
	}
}

/*
 * LZMA compressed segment format(simplified)
 * ------------------------------------------
 * Offset Size Description
 *  0     1   Special LZMA properties for compressed data
 *  1     4   Dictionary size (little endian)
 * 13         Compressed data
 *
 * Derived from http://docs.bugaco.com/7zip/lzma.txt
 * We do not store the uncompressed chunk size here. It is stored in
 * our chunk header.
 */
int
lzma_compress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data)
{
	size_t props_len = LZMA_PROPS_SIZE;
	SRes res;
	Byte *_dst;
	CLzmaEncProps *props = (CLzmaEncProps *)data;

	if (*dstlen < LZMA_PROPS_SIZE) {
		lzerr(SZ_ERROR_DESTLEN);
		return (-1);
	}
	props->level = level;

	_dst = (Byte *)dst;
	*dstlen -= LZMA_PROPS_SIZE;
	res = LzmaEncode(_dst + LZMA_PROPS_SIZE, dstlen, src, srclen,
	    props, _dst, &props_len, 0, NULL, &g_Alloc, &g_Alloc);

	if (res != 0) {
		lzerr(res);
		return (-1);
	}

	*dstlen += LZMA_PROPS_SIZE;
	return (0);
}

int
lzma_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data)
{
	size_t _srclen;
	const uchar_t *_src;
	SRes res;
	ELzmaStatus status;

	_srclen = srclen - LZMA_PROPS_SIZE;
	_src = (uchar_t *)src + LZMA_PROPS_SIZE;

	if ((res = LzmaDecode((uchar_t *)dst, dstlen, _src, &_srclen,
	    src, LZMA_PROPS_SIZE, LZMA_FINISH_ANY,
	    &status, &g_Alloc)) != SZ_OK) {
		lzerr(res);
		return (-1);
	}
	return (0);
}

