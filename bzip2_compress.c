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
#include <bzlib.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

/*
 * Max buffer size allowed for a single bzip2 compress/decompress call.
 */
#define	SINGLE_CALL_MAX (2147483648UL)

static void *
slab_alloc_i(void *p, int items, int size) {
	void *ptr;
	uint64_t tot = (uint64_t)items * (uint64_t)size;

	ptr = slab_alloc(p, tot);
	return (ptr);
}

void
bzip2_stats(int show)
{
}

void
bzip2_props(algo_props_t *data, int level, uint64_t chunksize) {
	data->delta2_span = 200;
	data->deltac_min_distance = FOURM;
}

int
bzip2_init(void **data, int *level, int nthreads, uint64_t chunksize,
	   int file_version, compress_op_t op)
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
bzip2_compress(void *src, uint64_t srclen, void *dst, uint64_t *dstlen,
	       int level, uchar_t chdr, void *data)
{
	bz_stream bzs;
	int ret, ending;
	unsigned int slen, dlen;
	uint64_t _srclen = srclen;
	uint64_t _dstlen = *dstlen;
	char *dst1 = (char *)dst;
	char *src1 = (char *)src;

	bzs.bzalloc = slab_alloc_i;
	bzs.bzfree = slab_free;
	bzs.opaque = NULL;

	ret = BZ2_bzCompressInit(&bzs, level, 0, 30);
	if (ret != BZ_OK) {
		bzerr(ret);
		return (-1);
	}

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

		bzs.next_in = src1;
		bzs.avail_in = slen;
		bzs.next_out = dst1;
		bzs.avail_out = dlen;
		if (!ending) {
			ret = BZ2_bzCompress(&bzs, BZ_RUN);
			if (ret != BZ_RUN_OK) {
				BZ2_bzCompressEnd(&bzs);
				return (-1);
			}
		} else {
			ret = BZ2_bzCompress(&bzs, BZ_FINISH);
			if (ret == BZ_FINISH_OK) {
				BZ2_bzCompressEnd(&bzs);
				return (-1);
			}
			if (ret != BZ_STREAM_END) {
				BZ2_bzCompressEnd(&bzs);
				return (-1);
			}
		}
		dst1 += (dlen - bzs.avail_out);
		_dstlen -= (dlen - bzs.avail_out);
		src1 += slen;
		_srclen -= slen;
	}

	/* normal termination */
	*dstlen = *dstlen - _dstlen;
	BZ2_bzCompressEnd(&bzs);
	return (0);
}

int
bzip2_decompress(void *src, uint64_t srclen, void *dst, uint64_t *dstlen,
		 int level, uchar_t chdr, void *data)
{
	bz_stream bzs;
	int ret;
	unsigned int slen, dlen;
	uint64_t _srclen = srclen;
	uint64_t _dstlen = *dstlen;
	char *dst1 = (char *)dst;
	char *src1 = (char *)src;

	bzs.bzalloc = slab_alloc_i;
	bzs.bzfree = slab_free;
	bzs.opaque = NULL;

	ret = BZ2_bzDecompressInit(&bzs, 0, 0);
	if (ret != BZ_OK) {
		bzerr(ret);
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

		bzs.next_in = src1;
		bzs.avail_in = slen;
		bzs.next_out = dst1;
		bzs.avail_out = dlen;

		ret = BZ2_bzDecompress(&bzs);
		if (ret != BZ_OK && ret != BZ_STREAM_END) {
			BZ2_bzDecompressEnd(&bzs);
			bzerr(ret);
			return (-1);
		}
		dst1 += (dlen - bzs.avail_out);
		_dstlen -= (dlen - bzs.avail_out);
		src1 += (slen - bzs.avail_in);
		_srclen -= (slen - bzs.avail_in);
	}

	/* normal termination */
	*dstlen = *dstlen - _dstlen;
	BZ2_bzDecompressEnd(&bzs);
	return (0);
}
