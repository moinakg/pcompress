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
#include <stdio.h>
#include <strings.h>
#include <bzlib.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

static void *
slab_alloc_i(void *p, int items, int size) {
	void *ptr;
	size_t tot = (size_t)items * (size_t)size;

	ptr = slab_alloc(p, tot);
	return (ptr);
}

int
bzip2_init(void **data, int *level, ssize_t chunksize)
{
	if (*level > 9) *level = 9;
	return (0);
}

static void
bzerr(int err)
{
	switch (err) {
	    case BZ_SEQUENCE_ERROR:
		fprintf(stderr, "Bzip2: Call sequence error, buggy code ?\n");
		break;
	    case BZ_PARAM_ERROR:
		fprintf(stderr, "Bzip2: Invalid parameter\n");
		break;
	    case BZ_MEM_ERROR:
		fprintf(stderr, "Bzip2: Out of memory\n");
		break;
	    case BZ_DATA_ERROR:
		fprintf(stderr, "Bzip2: Data integrity checksum error\n");
		break;
	    case BZ_DATA_ERROR_MAGIC:
		fprintf(stderr, "Bzip2: Invalid magic number in compressed buf\n");
		break;
	    case BZ_OUTBUFF_FULL:
		fprintf(stderr, "Bzip2: Output buffer overflow\n");
		break;
	    case BZ_CONFIG_ERROR:
		fprintf(stderr, "Bzip2: Improper library config on platform\n");
		break;
	    default:
		fprintf(stderr, "Bzip2: Unknown error code: %d\n", err);
	}
}

int
bzip2_compress(void *src, size_t srclen, void *dst, size_t *dstlen, int level, void *data)
{
	bz_stream bzs;
	int ret;
	unsigned int _dstlen;

	bzs.bzalloc = slab_alloc_i;
	bzs.bzfree = slab_free;
	bzs.opaque = NULL;

	ret = BZ2_bzCompressInit(&bzs, level, 0, 30);
	if (ret != BZ_OK) {
		bzerr(ret);
		return (-1);
	}

	bzs.next_in = src;
	bzs.avail_in = srclen;
	bzs.next_out = dst;
	bzs.avail_out = *dstlen;

	ret = BZ2_bzCompress(&bzs, BZ_FINISH);
	if (ret == BZ_FINISH_OK) {
		BZ2_bzCompressEnd(&bzs);
		return (-1);
	}
	if (ret != BZ_STREAM_END) {
		BZ2_bzCompressEnd(&bzs);
		bzerr(ret);
		return (-1);
	}

	/* normal termination */
	*dstlen -= bzs.avail_out;
	BZ2_bzCompressEnd(&bzs);
	return (0);
}

int
bzip2_decompress(void *src, size_t srclen, void *dst, size_t *dstlen, int level, void *data)
{
	bz_stream bzs;
	int ret;

	bzs.bzalloc = slab_alloc_i;
	bzs.bzfree = slab_free;
	bzs.opaque = NULL;

	ret = BZ2_bzDecompressInit(&bzs, 0, 0);
	if (ret != BZ_OK) {
		bzerr(ret);
		return (-1);
	}

	bzs.next_in = (uchar_t *)src + CHDR_SZ;
	bzs.avail_in = srclen;
	bzs.next_out = dst;
	bzs.avail_out = *dstlen;

	ret = BZ2_bzDecompress(&bzs);
	if (ret == BZ_FINISH_OK) {
		BZ2_bzDecompressEnd(&bzs);
		bzerr(ret);
		return (-1);
	}
	if (ret != BZ_STREAM_END) {
		BZ2_bzDecompressEnd(&bzs);
		bzerr(ret);
		return (-1);
	}

	/* normal termination */
	*dstlen -= bzs.avail_out;
	BZ2_bzDecompressEnd(&bzs);
	return (0);
}
