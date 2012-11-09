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

#include <stdio.h>
#include <sys/types.h>
#include <strings.h>
#include <zlib.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

/*
 * Max buffer size allowed for a single zlib compress/decompress call.
 */
#define	SINGLE_CALL_MAX (2147483648UL)

static void zerr(int ret, int cmp);

static void *
slab_alloc_ui(void *p, unsigned int items, unsigned int size) {
	void *ptr;
	size_t tot = (size_t)items * (size_t)size;

	ptr = slab_alloc(p, tot);
	return (ptr);
}

uint32_t
zlib_buf_extra(ssize_t buflen)
{
	if (buflen > SINGLE_CALL_MAX)
		buflen = SINGLE_CALL_MAX;
	return (compressBound(buflen) - buflen);
}

int
zlib_init(void **data, int *level, int nthreads, ssize_t chunksize)
{
	z_stream *zs;
	int ret;

	zs = slab_alloc(NULL, sizeof (z_stream));
	zs->zalloc = slab_alloc_ui;
	zs->zfree = slab_free;
	zs->opaque = NULL;

	if (*level > 9) *level = 9;
	ret = deflateInit(zs, *level);
	if (ret != Z_OK) {
		zerr(ret, 0);
		return (-1);
	}

	*data = zs;
	return (0);
}

void
zlib_stats(int show)
{
}

int
zlib_deinit(void **data)
{
	if (*data) {
		z_stream *zs = (z_stream *)(*data);
		deflateEnd(zs);
		slab_free(NULL, *data);
	}
}

static
void zerr(int ret, int cmp)
{
	switch (ret) {
	    case Z_ERRNO:
		perror(" ");
		break;
	    case Z_STREAM_ERROR:
		fprintf(stderr, "Zlib: Invalid stream structure\n");
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
	    case Z_BUF_ERROR:
		/* This error is non-fatal during compression. */
		if (!cmp)
			fprintf(stderr, "Zlib: Buffer error decompression failed.\n");
		break;
	    case Z_NEED_DICT:
		fprintf(stderr, "Zlib: Need present dictionary.\n");
		break;
	    default:
		fprintf(stderr, "Zlib: Unknown error code: %d\n", ret);
	}
}

int
zlib_compress(void *src, size_t srclen, void *dst, size_t *dstlen,
	      int level, uchar_t chdr, void *data)
{
	int ret, ending;
	unsigned int slen, dlen;
	size_t _srclen = srclen;
	size_t _dstlen = *dstlen;
	uchar_t *dst1 = dst;
	uchar_t *src1 = src;
	z_stream *zs = (z_stream *)data;

	ending = 0;
	while (_srclen > 0) {
		if (_srclen > SINGLE_CALL_MAX) {
			slen = SINGLE_CALL_MAX;
		} else {
			slen = _srclen;
			ending = 1;
		}
		if (_dstlen > SINGLE_CALL_MAX) {
			dlen = SINGLE_CALL_MAX;
		} else {
			dlen = _dstlen;
		}

		zs->next_in = src1;
		zs->avail_in = slen;
		zs->next_out = dst1;
		zs->avail_out = dlen;
		if (!ending) {
			ret = deflate(zs, Z_NO_FLUSH);
			if (ret != Z_OK) {
				deflateReset(zs);
				zerr(ret, 1);
				return (-1);
			}
		} else {
			ret = deflate(zs, Z_FINISH);
			if (ret != Z_STREAM_END) {
				deflateReset(zs);
				if (ret == Z_OK)
					zerr(Z_BUF_ERROR, 1);
				else
					zerr(ret, 1);
				return (-1);
			}
		}
		dst1 += (dlen - zs->avail_out);
		_dstlen -= (dlen - zs->avail_out);
		src1 += slen;
		_srclen -= slen;
	}

	*dstlen = *dstlen - _dstlen;
	ret = deflateReset(zs);
	if (ret != Z_OK) {
		zerr(ret, 1);
		return (-1);
	}
	return (0);
}

int
zlib_decompress(void *src, size_t srclen, void *dst, size_t *dstlen,
		int level, uchar_t chdr, void *data)
{
	z_stream zs;
	int err;
	unsigned int slen, dlen;
	size_t _srclen = srclen;
	size_t _dstlen = *dstlen;
	uchar_t *dst1 = dst;
	uchar_t *src1 = src;

	zs.zalloc = slab_alloc_ui;
	zs.zfree = slab_free;
	zs.opaque = NULL;

	if ((err = inflateInit(&zs)) != Z_OK) {
		zerr(err, 0);
		return (-1);
	}

	while (_srclen > 0) {
		if (_srclen > SINGLE_CALL_MAX) {
			slen = SINGLE_CALL_MAX;
		} else {
			slen = _srclen;
		}
		if (_dstlen > SINGLE_CALL_MAX) {
			dlen = SINGLE_CALL_MAX;
		} else {
			dlen = _dstlen;
		}

		zs.next_in = src1;
		zs.avail_in = slen;
		zs.next_out = dst1;
		zs.avail_out = dlen;

		err = inflate(&zs, Z_NO_FLUSH);
		if (err != Z_OK && err != Z_STREAM_END) {
			zerr(err, 0);
			return (-1);
		}

		dst1 += (dlen - zs.avail_out);
		_dstlen -= (dlen - zs.avail_out);
		src1 += (slen - zs.avail_in);
		_srclen -= (slen - zs.avail_in);

		if (err == Z_STREAM_END) {
			if (_srclen > 0) {
				zerr(Z_DATA_ERROR, 0);
				return (-1);
			} else {
				break;
			}
		}
	}

	*dstlen = *dstlen - _dstlen;
	inflateEnd(&zs);
	return (0);
}
