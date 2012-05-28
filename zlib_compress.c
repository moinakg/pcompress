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

#include <stdio.h>
#include <sys/types.h>
#include <strings.h>
#include <zlib.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

static void *
slab_alloc_ui(void *p, unsigned int items, unsigned int size) {
	void *ptr;
	size_t tot = (size_t)items * (size_t)size;

	ptr = slab_alloc(p, tot);
	return (ptr);
}

int
zlib_init(void **data, int *level, ssize_t chunksize)
{
	if (*level > 9) *level = 9;
	return (0);
}

static
void zerr(int ret)
{
	switch (ret) {
	    case Z_ERRNO:
		perror(" ");
		break;
	    case Z_STREAM_ERROR:
		fprintf(stderr, "Zlib: Invalid compression level\n");
		break;
	    case Z_DATA_ERROR:
		fprintf(stderr, "Zlib: Invalid or incomplete deflate data\n");
		break;
	    case Z_MEM_ERROR:
		fprintf(stderr, "Zlib: Out of memory\n");
		break;
	    case Z_VERSION_ERROR:
		fprintf(stderr, "Zlib: Version mismatch!\n");
		break;
	    default:
		fprintf(stderr, "Zlib: Unknown error code: %d\n", ret);
	}
}

int
zlib_compress(void *src, size_t srclen, void *dst, size_t *dstlen, int level, void *data)
{
	z_stream zs;
	int ret;

	zs.next_in = src;
	zs.avail_in = srclen;
	zs.next_out = dst;
	zs.avail_out = *dstlen;

	zs.zalloc = slab_alloc_ui;
	zs.zfree = slab_free;
	zs.opaque = NULL;

	ret = deflateInit(&zs, level);
	if (ret != Z_OK) {
		zerr(ret);
		return (-1);
	}

	ret = deflate(&zs, Z_FINISH);
	if (ret != Z_STREAM_END) {
		deflateEnd(&zs);
		if (ret == Z_OK)
			zerr(Z_BUF_ERROR);
		else
			zerr(ret);
		return (-1);
	}
	*dstlen = zs.total_out;
	ret = deflateEnd(&zs);
	if (ret != Z_OK) {
		zerr(ret);
		return (-1);
	}
	return (0);
}

int
zlib_decompress(void *src, size_t srclen, void *dst, size_t *dstlen, int level, void *data)
{
	z_stream zs;
	int err;

	bzero(&zs, sizeof (zs));
	zs.next_in = (unsigned char *)src + CHDR_SZ;
	zs.avail_in = srclen;
	zs.next_out = dst;
	zs.avail_out = *dstlen;

	zs.zalloc = slab_alloc_ui;
	zs.zfree = slab_free;
	zs.opaque = NULL;

	if ((err = inflateInit(&zs)) != Z_OK) {
		zerr(err);
		return (-1);
	}

	if ((err = inflate(&zs, Z_FINISH)) != Z_STREAM_END) {
		inflateEnd(&zs);
		if (err == Z_OK)
			zerr(Z_BUF_ERROR);
		else
			zerr(err);
		return (-1);
	}

	*dstlen = zs.total_out;
	inflateEnd(&zs);
	return (0);
}
