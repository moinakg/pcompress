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

void
none_stats(int show)
{
}

int
none_init(void **data, int *level, ssize_t chunksize)
{
	return (0);
}

int
none_deinit(void **data)
{
	return (0);
}

int
none_compress(void *src, size_t srclen, void *dst, size_t *dstlen,
	       int level, uchar_t chdr, void *data)
{
	memcpy(dst, src, srclen);
	return (0);
}

int
none_decompress(void *src, size_t srclen, void *dst, size_t *dstlen,
		 int level, uchar_t chdr, void *data)
{
	memcpy(dst, src, srclen);
	return (0);
}
