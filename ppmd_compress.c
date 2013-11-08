/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
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
static unsigned int ppmd8_mem_sz[] = {
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

void
ppmd_props(algo_props_t *data, int level, uint64_t chunksize) {
	data->delta2_span = 100;
	data->deltac_min_distance = FOURM;
}

int
ppmd_init(void **data, int *level, int nthreads, uint64_t chunksize,
	  int file_version, compress_op_t op)
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
		log_msg(LOG_ERR, 0, "PPMD: Out of memory.\n");
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
ppmd_compress(void *src, uint64_t srclen, void *dst,
 	      uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data)
{
	CPpmd8 *_ppmd = (CPpmd8 *)data;
	uchar_t *_src = (uchar_t *)src;

	if (btype & TYPE_COMPRESSED)
		return (-1);
	Ppmd8_RangeEnc_Init(_ppmd);
	Ppmd8_Init(_ppmd, _ppmd->Order, PPMD8_RESTORE_METHOD_RESTART);
	_ppmd->buf = (Byte *)dst;
	_ppmd->bufLen = *dstlen;
	_ppmd->bufUsed = 0;
	_ppmd->overflow = 0;

	Ppmd8_EncodeBuffer(_ppmd, _src, srclen);
	Ppmd8_EncodeSymbol(_ppmd, -1);
	Ppmd8_RangeEnc_FlushData(_ppmd);

	if (_ppmd->overflow) return (-1);
	*dstlen = _ppmd->bufUsed;
	return (0);
}

int
ppmd_decompress(void *src, uint64_t srclen, void *dst,
		uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data)
{
	CPpmd8 *_ppmd = (CPpmd8 *)data;
	Byte *_src = (Byte *)src;
	Byte *_dst = (Byte *)dst;
	uint64_t i;
	int res;

	_ppmd->buf = (Byte *)_src;
	_ppmd->bufLen = srclen;
	_ppmd->bufUsed = 0;
	_ppmd->overflow = 0;
	Ppmd8_RangeDec_Init(_ppmd);
	Ppmd8_Init(_ppmd, _ppmd->Order, PPMD8_RESTORE_METHOD_RESTART);

	res = Ppmd8_DecodeToBuffer(_ppmd, _dst, *dstlen, &i);
	if (res < 0 && res != -1) {
		if (Ppmd8_DecodeSymbol(_ppmd) != -1)
			return (-1);
		i++;
	}

	if (i < *dstlen || _ppmd->overflow)
		return (-1);
	return (0);
}
