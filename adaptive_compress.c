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

extern int lzma_init(void **data, int *level, int nthreads, ssize_t chunksize);
extern int lzma_deinit(void **data);
extern int ppmd_init(void **data, int *level, int nthreads, ssize_t chunksize);
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
adapt_init(void **data, int *level, int nthreads, ssize_t chunksize)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv;

	if (!adat) {
		adat = (struct adapt_data *)slab_alloc(NULL, sizeof (struct adapt_data));
		adat->adapt_mode = 1;
		rv = ppmd_init(&(adat->ppmd_data), level, nthreads, chunksize);
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
adapt2_init(void **data, int *level, int nthreads, ssize_t chunksize)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv, lv;

	if (!adat) {
		adat = (struct adapt_data *)slab_alloc(NULL, sizeof (struct adapt_data));
		adat->adapt_mode = 2;
		adat->ppmd_data = NULL;
		lv = *level;
		rv = ppmd_init(&(adat->ppmd_data), &lv, nthreads, chunksize);
		lv = *level;
		if (rv == 0)
			rv = lzma_init(&(adat->lzma_data), &lv, nthreads, chunksize);
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
	uchar_t *src1 = (uchar_t *)src;
	size_t i, bincount;
	int rv;

	/*
	 * Count number of 8-bit binary bytes in source.
	 */
	bincount = 0;
	for (i = 0; i < srclen; i++)
		bincount += (src1[i] >> 7);

	/*
	 * Use PPMd if at least 70% of source is 7-bit textual bytes, otherwise
	 * use Bzip2 or LZMA.
	 */
	if (bincount > (srclen / 10 * 3)) {
		if (adat->adapt_mode == 2) {
			rv = lzma_compress(src, srclen, dst, dstlen, level, chdr, adat->lzma_data);
			if (rv < 0)
				return (rv);
			rv = COMPRESS_LZMA;
			lzma_count++;
		} else {
			rv = bzip2_compress(src, srclen, dst, dstlen, level, chdr, NULL);
			if (rv < 0)
				return (rv);
			rv = COMPRESS_BZIP2;
			bzip2_count++;
		}
	} else {
		rv = ppmd_compress(src, srclen, dst, dstlen, level, chdr, adat->ppmd_data);
		if (rv < 0)
			return (rv);
		rv = COMPRESS_PPMD;
		ppmd_count++;
	}

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
