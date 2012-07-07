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
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>
#include <Ppmd8.h>

/*
 * PPMd model order to working set memory size mappings.
 */
unsigned int ppmd8_mem_sz[] = {
	(16 << 20),
	(16 << 20),
	(32 << 20),
	(32 << 20),
	(64 << 20),
	(64 << 20),
	(100 << 20),
	(100 << 20),
	(400 << 20),
	(400 << 20),
	(700 << 20),
	(700 << 20),
	(700 << 20),
	(1200 << 20),
	(1200 << 20)
};

static ISzAlloc g_Alloc = {
	slab_alloc,
	slab_free,
	NULL
};

void
ppmd_stats(int show)
{
}

int
ppmd_init(void **data, int *level, ssize_t chunksize)
{
	CPpmd8 *_ppmd;

	_ppmd = (CPpmd8 *)slab_alloc(NULL, sizeof (CPpmd8));
	if (!_ppmd)
		return (-1);

	/* Levels 0 - 14 correspond to PPMd model orders 0 - 14. */
	if (*level > 14) *level = 14;
	_ppmd->Order = *level;

	_ppmd->Base = 0;
	_ppmd->Size = 0;
	if (!Ppmd8_Alloc(_ppmd, ppmd8_mem_sz[*level], &g_Alloc)) {
		fprintf(stderr, "Out of memory.\n");
		return (-1);
	}
	Ppmd8_Construct(_ppmd);
	*data = _ppmd;
	if (*level > 9) *level = 9;
	return (0);
}

int
ppmd_deinit(void **data)
{
	CPpmd8 *_ppmd = (CPpmd8 *)(*data);
	if (_ppmd) {
		Ppmd8_Free(_ppmd, &g_Alloc);
		slab_free(NULL, _ppmd);
	}
	*data = NULL;
	return (0);
}

int
ppmd_compress(void *src, size_t srclen, void *dst,
 	      size_t *dstlen, int level, uchar_t chdr, void *data)
{
	CPpmd8 *_ppmd = (CPpmd8 *)data;
	uchar_t *_src = (uchar_t *)src;
	UInt32 i;

	Ppmd8_RangeEnc_Init(_ppmd);
	Ppmd8_Init(_ppmd, _ppmd->Order, PPMD8_RESTORE_METHOD_RESTART);
	_ppmd->buf = (Byte *)dst;
	_ppmd->bufLen = *dstlen;
	_ppmd->bufUsed = 0;

	Ppmd8_EncodeBuffer(_ppmd, _src, srclen);
	Ppmd8_EncodeSymbol(_ppmd, -1);
	Ppmd8_RangeEnc_FlushData(_ppmd);

	*dstlen = _ppmd->bufUsed;
	return (0);
}

int
ppmd_decompress(void *src, size_t srclen, void *dst,
		size_t *dstlen, int level, uchar_t chdr, void *data)
{
	CPpmd8 *_ppmd = (CPpmd8 *)data;
	Byte *_src = (Byte *)src;
	Byte *_dst = (Byte *)dst;
	size_t i;
	int res;

	if (*((char *)_src) < 2)
		return (-1);

	_ppmd->buf = (Byte *)_src;
	_ppmd->bufLen = srclen;
	_ppmd->bufUsed = 0;
	Ppmd8_RangeDec_Init(_ppmd);
	Ppmd8_Init(_ppmd, _ppmd->Order, PPMD8_RESTORE_METHOD_RESTART);

	res = Ppmd8_DecodeToBuffer(_ppmd, _dst, *dstlen, &i);
	if (res < 0 && res != -1) {
		if (Ppmd8_DecodeSymbol(_ppmd) != -1)
			return (-1);
		i++;
	}

	if (i < *dstlen)
		return (-1);
	return (0);
}
