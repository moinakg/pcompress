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
 * version 2.1 of the License, or (at your option) any later version.
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
#include <byteswap.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

extern int lzma_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, void *data);
extern int bzip2_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, void *data);
extern int ppmd_compress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);

extern int lzma_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);
extern int bzip2_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);
extern int ppmd_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);

extern int lzma_init(void **data, int *level, ssize_t chunksize);
extern int lzma_deinit(void **data);
extern int ppmd_init(void **data, int *level, ssize_t chunksize);
extern int ppmd_deinit(void **data);

struct adapt_data {
	void *lzma_data;
	void *ppmd_data;
	int adapt_mode;
};

int
adapt_init(void **data, int *level, ssize_t chunksize)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv;

	if (!adat) {
		adat = (struct adapt_data *)slab_alloc(NULL, sizeof (struct adapt_data));
		adat->adapt_mode = 1;
		adat->ppmd_data = NULL;
		rv = lzma_init(&(adat->lzma_data), level, chunksize);
		*data = adat;
		if (*level > 9) *level = 9;
	}
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
		rv = lzma_init(&(adat->lzma_data), &lv, chunksize);
		lv = *level;
		if (rv == 0)
			ppmd_init(&(adat->ppmd_data), &lv, chunksize);
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
		rv = lzma_deinit(&(adat->lzma_data));
		if (adat->ppmd_data)
			rv += ppmd_deinit(&(adat->ppmd_data));
		slab_free(NULL, adat);
		*data = NULL;
	}
	return (rv);
}

int
adapt_compress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data)
{
	struct adapt_data *adat = (struct adapt_data *)(data);
	int rv, rv1, rv2;
	size_t dst2len, dst3len, smaller_dstlen;
	uchar_t *dst2, *smaller_dst, *larger_dst;
	void *tmp;

	dst2 = slab_alloc(NULL, *dstlen);
	if (!dst2) {
		fprintf(stderr, "Adapt: Out of memory\n");
		return (-1);
	}

	rv = COMPRESS_LZMA;
	dst2len = *dstlen;
	dst3len = *dstlen;
	rv1 = bzip2_compress(src, srclen, dst2, &dst2len, level, data);
	if (rv1 < 0) dst2len = dst3len;
	rv2 = lzma_compress(src, srclen, dst, dstlen, level, adat->lzma_data);
	if (rv2 < 0) *dstlen = dst3len;

	if (dst2len < *dstlen) {
		rv = COMPRESS_BZIP2;
		larger_dst = dst;
		smaller_dstlen = dst2len;
		smaller_dst = dst2;
	} else {
		larger_dst = dst2;
		smaller_dstlen = *dstlen;
		smaller_dst = dst;
	}

	if (adat->adapt_mode == 2) {
		rv2 = ppmd_compress(src, srclen, larger_dst, &dst2len,
		    level, adat->ppmd_data);
		if (rv2 < 0) dst2len = dst3len;
		if (dst2len < smaller_dstlen) {
			rv = COMPRESS_PPMD;
			smaller_dstlen = dst2len;
			smaller_dst = larger_dst;
		}
	}

	if (smaller_dst != dst) {
		memcpy(dst, smaller_dst, smaller_dstlen);
		*dstlen = smaller_dstlen;
	}
	slab_free(NULL, dst2);
	return (rv);
}

int
adapt_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data)
{
	struct adapt_data *adat = (struct adapt_data *)(data);
	uchar_t HDR;

	HDR = *((uchar_t *)src);

	if (HDR & (COMPRESS_LZMA << 4)) {
		return (lzma_decompress(src, srclen, dst, dstlen, level, adat->lzma_data));

	} else if (HDR & (COMPRESS_BZIP2 << 4)) {
		return (bzip2_decompress(src, srclen, dst, dstlen, level, NULL));

	} else if (HDR & (COMPRESS_PPMD << 4)) {
		return (ppmd_decompress(src, srclen, dst, dstlen, level, adat->ppmd_data));

	} else {
		fprintf(stderr, "Unrecognized compression mode, file corrupt.\n");
	}
	return (-1);
}
