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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#if defined(sun) || defined(__sun)
#include <sys/byteorder.h>
#else
#include <byteswap.h>
#endif
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

static unsigned int lzma_count = 0;
static unsigned int bzip2_count = 0;
static unsigned int ppmd_count = 0;

extern int lzma_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, uchar_t chdr, void *data);
extern int bzip2_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, uchar_t chdr, void *data);
extern int ppmd_compress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data);

extern int lzma_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data);
extern int bzip2_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data);
extern int ppmd_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data);

extern int lzma_init(void **data, int *level, ssize_t chunksize);
extern int lzma_deinit(void **data);
extern int ppmd_init(void **data, int *level, ssize_t chunksize);
extern int ppmd_deinit(void **data);

struct adapt_data {
	void *lzma_data;
	void *ppmd_data;
	int adapt_mode;
};

void
adapt_stats(int show)
{
	if (show) {
		fprintf(stderr, "Adaptive mode stats:\n");
		fprintf(stderr, "	BZIP2 chunk count: %u\n", bzip2_count);
		fprintf(stderr, "	PPMd chunk count: %u\n", ppmd_count);
		fprintf(stderr, "	LZMA chunk count: %u\n\n", lzma_count);
	}
	lzma_count = 0;
	bzip2_count = 0;
	ppmd_count = 0;
}

int
adapt_init(void **data, int *level, ssize_t chunksize)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv;

	if (!adat) {
		adat = (struct adapt_data *)slab_alloc(NULL, sizeof (struct adapt_data));
		adat->adapt_mode = 1;
		rv = ppmd_init(&(adat->ppmd_data), level, chunksize);
		adat->lzma_data = NULL;
		*data = adat;
		if (*level > 9) *level = 9;
	}
	lzma_count = 0;
	bzip2_count = 0;
	ppmd_count = 0;
	return (rv);
}

int
adapt2_init(void **data, int *level, ssize_t chunksize)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv, lv;

	if (!adat) {
		adat = (struct adapt_data *)slab_alloc(NULL, sizeof (struct adapt_data));
		adat->adapt_mode = 2;
		adat->ppmd_data = NULL;
		lv = *level;
		rv = ppmd_init(&(adat->ppmd_data), &lv, chunksize);
		lv = *level;
		if (rv == 0)
			rv = lzma_init(&(adat->lzma_data), &lv, chunksize);
		*data = adat;
		if (*level > 9) *level = 9;
	}
	return (rv);
}

int
adapt_deinit(void **data)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv;

	if (adat) {
		rv = ppmd_deinit(&(adat->ppmd_data));
		if (adat->lzma_data)
			rv += lzma_deinit(&(adat->lzma_data));
		slab_free(NULL, adat);
		*data = NULL;
	}
	return (rv);
}

int
adapt_compress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data)
{
	struct adapt_data *adat = (struct adapt_data *)(data);
	int rv, rv1, rv2;
	unsigned int *inc;
	size_t dst2len, dst3len, smaller_dstlen;
	uchar_t *dst2, *smaller_dst;
	void *tmp;

	dst2 = slab_alloc(NULL, *dstlen);
	if (!dst2) {
		fprintf(stderr, "Adapt: Out of memory\n");
		return (-1);
	}

	rv = COMPRESS_PPMD;
	inc = &ppmd_count;
	dst2len = *dstlen;
	dst3len = *dstlen;
	rv1 = ppmd_compress(src, srclen, dst, dstlen, level, chdr, adat->ppmd_data);
	if (rv1 < 0) *dstlen = dst3len;

	if (adat->adapt_mode == 2) {
		rv2 = lzma_compress(src, srclen, dst2, &dst2len, level, chdr, adat->lzma_data);
		if (rv2 < 0) dst2len = dst3len;
		if (dst2len < *dstlen) {
			inc = &lzma_count;
			rv = COMPRESS_LZMA;
		}
	} else {
		rv2 = bzip2_compress(src, srclen, dst2, &dst2len, level, chdr, NULL);
		if (rv2 < 0) dst2len = dst3len;
		if (dst2len < *dstlen) {
			inc = &bzip2_count;
			rv = COMPRESS_BZIP2;
		}
	}

	if (dst2len < *dstlen) {
		smaller_dstlen = dst2len;
		smaller_dst = dst2;
	} else {
		smaller_dstlen = *dstlen;
		smaller_dst = dst;
	}

	*inc += 1;
	if (smaller_dst != dst) {
		memcpy(dst, smaller_dst, smaller_dstlen);
		*dstlen = smaller_dstlen;
	}
	slab_free(NULL, dst2);
	return (rv);
}

int
adapt_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data)
{
	struct adapt_data *adat = (struct adapt_data *)(data);
	uchar_t cmp_flags;

	cmp_flags = (chdr>>4) & CHDR_ALGO_MASK;

	if (cmp_flags == COMPRESS_LZMA) {
		return (lzma_decompress(src, srclen, dst, dstlen, level, chdr, adat->lzma_data));

	} else if (cmp_flags == COMPRESS_BZIP2) {
		return (bzip2_decompress(src, srclen, dst, dstlen, level, chdr, NULL));

	} else if (cmp_flags == COMPRESS_PPMD) {
		return (ppmd_decompress(src, srclen, dst, dstlen, level, chdr, adat->ppmd_data));

	} else {
		fprintf(stderr, "Unrecognized compression mode, file corrupt.\n");
	}
	return (-1);
}
