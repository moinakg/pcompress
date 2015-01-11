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
 */

#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
/*
#if defined(sun) || defined(__sun)
#include <sys/byteorder.h>
#else
#include <byteswap.h>
#endif
*/
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>
#include <pc_archive.h>
#include "filters/analyzer/analyzer.h"

static unsigned int lzma_count = 0;
static unsigned int bzip2_count = 0;
static unsigned int bsc_count = 0;
static unsigned int ppmd_count = 0;
static unsigned int lz4_count = 0;

extern int lzma_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *destlen, int level, uchar_t chdr, int btype, void *data);
extern int bzip2_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *destlen, int level, uchar_t chdr, int btype, void *data);
extern int ppmd_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int libbsc_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int lz4_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);

extern int lzma_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int bzip2_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int ppmd_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int libbsc_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int lz4_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);

extern int lzma_init(void **data, int *level, int nthreads, uint64_t chunksize,
		     int file_version, compress_op_t op);
extern int lzma_deinit(void **data);
extern int ppmd_init(void **data, int *level, int nthreads, uint64_t chunksize,
		     int file_version, compress_op_t op);
extern int ppmd_deinit(void **data);
extern int libbsc_init(void **data, int *level, int nthreads, uint64_t chunksize,
		       int file_version, compress_op_t op);
extern int libbsc_deinit(void **data);
extern int lz4_init(void **data, int *level, int nthreads, uint64_t chunksize,
		       int file_version, compress_op_t op);
extern int lz4_deinit(void **data);

extern int ppmd_alloc(void *data);
extern void ppmd_free(void *data);
extern int ppmd_state_init(void **data, int *level, int alloc);

extern int lz4_buf_extra(uint64_t buflen);
extern int libbsc_buf_extra(uint64_t buflen);

struct adapt_data {
	void *lzma_data;
	void *ppmd_data;
	void *bsc_data;
	void *lz4_data;
	int adapt_mode;
	analyzer_ctx_t *actx;
};

void
adapt_set_analyzer_ctx(void *data, analyzer_ctx_t *actx)
{
	struct adapt_data *adat = (struct adapt_data *)data;
	adat->actx = actx;
}

void
adapt_stats(int show)
{
	if (show) {
		if (bzip2_count > 0 || bsc_count > 0 || ppmd_count > 0 || lzma_count > 0) {
			log_msg(LOG_INFO, 0, "Adaptive mode stats:");
			log_msg(LOG_INFO, 0, "	BZIP2 chunk count: %u", bzip2_count);
			log_msg(LOG_INFO, 0, "	LIBBSC chunk count: %u", bsc_count);
			log_msg(LOG_INFO, 0, "	PPMd chunk count: %u", ppmd_count);
			log_msg(LOG_INFO, 0, "	LZMA chunk count: %u", lzma_count);
			log_msg(LOG_INFO, 0, "	LZ4 chunk count: %u", lz4_count);
		} else {
			log_msg(LOG_INFO, 0, "\n");
		}
	}
	lzma_count = 0;
	bzip2_count = 0;
	bsc_count = 0;
	ppmd_count = 0;
	lz4_count = 0;
}

void
adapt_props(algo_props_t *data, int level, uint64_t chunksize)
{
	int ext1, ext2;

	data->delta2_span = 200;
	data->deltac_min_distance = EIGHTM;
	ext1 = lz4_buf_extra(chunksize);

#ifdef ENABLE_PC_LIBBSC
	ext2 = libbsc_buf_extra(chunksize);
	if (ext2 > ext1) ext1 = ext2;
#endif

	data->buf_extra = ext1;
}

int
adapt_init(void **data, int *level, int nthreads, uint64_t chunksize,
	   int file_version, compress_op_t op)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv = 0, lv = 1;

	if (!adat) {
		adat = (struct adapt_data *)slab_alloc(NULL, sizeof (struct adapt_data));
		adat->adapt_mode = 1;
		rv = ppmd_state_init(&(adat->ppmd_data), level, 0);

		/*
		 * LZ4 is used to tackle some embedded archive headers and/or zero paddings in
		 * otherwise incompressible data. So we always use it at the lowest and fastest
		 * compression level.
		 */
		if (rv == 0)
			rv = lz4_init(&(adat->lz4_data), &lv, nthreads, chunksize, file_version, op);
		adat->lzma_data = NULL;
		adat->bsc_data = NULL;
		*data = adat;
		if (*level > 9) *level = 9;
	}
	lzma_count = 0;
	bzip2_count = 0;
	ppmd_count = 0;
	bsc_count = 0;
	lz4_count = 0;
	return (rv);
}

int
adapt2_init(void **data, int *level, int nthreads, uint64_t chunksize,
	    int file_version, compress_op_t op)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv = 0, lv;

	if (!adat) {
		adat = (struct adapt_data *)slab_alloc(NULL, sizeof (struct adapt_data));
		adat->adapt_mode = 2;
		adat->ppmd_data = NULL;
		adat->bsc_data = NULL;
		lv = *level;
		if (lv > 10) lv = 10;
		rv = ppmd_state_init(&(adat->ppmd_data), level, 0);
		lv = *level;
		if (rv == 0)
			rv = lzma_init(&(adat->lzma_data), &lv, nthreads, chunksize, file_version, op);
		lv = *level;
#ifdef ENABLE_PC_LIBBSC
		if (rv == 0)
			rv = libbsc_init(&(adat->bsc_data), &lv, nthreads, chunksize, file_version, op);
#endif
		/*
		 * LZ4 is used to tackle some embedded archive headers and/or zero paddings in
		 * otherwise incompressible data. So we always use it at the lowest and fastest
		 * compression level.
		 */
		lv = 1;
		if (rv == 0)
			rv = lz4_init(&(adat->lz4_data), &lv, nthreads, chunksize, file_version, op);
		*data = adat;
		if (*level > 9) *level = 9;
	}
	lzma_count = 0;
	bzip2_count = 0;
	ppmd_count = 0;
	bsc_count = 0;
	lz4_count = 0;
	return (rv);
}

int
adapt_deinit(void **data)
{
	struct adapt_data *adat = (struct adapt_data *)(*data);
	int rv = 0;

	if (adat) {
		rv = ppmd_deinit(&(adat->ppmd_data));
		if (adat->lzma_data)
			rv += lzma_deinit(&(adat->lzma_data));
		if (adat->lz4_data)
			rv += lz4_deinit(&(adat->lz4_data));
		slab_free(NULL, adat);
		*data = NULL;
	}
	return (rv);
}

/*
 * Identify the types that BSC can compress better than others.
 */
int
is_bsc_type(int btype)
{
	int stype = PC_SUBTYPE(btype);
	int mtype = PC_TYPE(btype);

	return ((stype == TYPE_BMP) | (stype == TYPE_DNA_SEQ) |
	    (stype == TYPE_MP4) | (stype == TYPE_FLAC) | (stype == TYPE_AVI) |
	    (stype == TYPE_DICOM) | (stype == TYPE_MEDIA_BSC) |
	    (mtype == TYPE_TEXT && stype != TYPE_MARKUP));
}

int
adapt_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data)
{
	struct adapt_data *adat = (struct adapt_data *)(data);
	int rv = 0, bsc_type = 0;
	int stype = PC_SUBTYPE(btype);
	analyzer_ctx_t actx;

	if (btype == TYPE_UNKNOWN || PC_TYPE(btype) == TYPE_TEXT ||
	    stype == TYPE_ARCHIVE_TAR || stype == TYPE_PDF) {
		if (adat->actx == NULL) {
			analyze_buffer(src, srclen, &actx);
			adat->actx = &actx;
		}
		if (adat->adapt_mode == 2) {
			btype = adat->actx->forty_pct.btype;

		} else if (adat->adapt_mode == 1) {
			btype = adat->actx->fifty_pct.btype;
		}
	}

	/* Reset analyzer context for subsequent calls. */
	adat->actx = NULL;

	/*
	 * Use PPMd if some percentage of source is 7-bit textual bytes, otherwise
	 * use Bzip2 or LZMA. For totally incompressible data we always use LZ4. There
	 * is no point trying to compress such data, like Jpegs. However some archive headers
	 * and zero paddings can exist which LZ4 can easily take care of very fast.
	 */
#ifdef ENABLE_PC_LIBBSC
	bsc_type = is_bsc_type(btype);
#endif
	if (is_incompressible(btype) && !bsc_type) {
		rv = lz4_compress(src, srclen, dst, dstlen, level, chdr, btype, adat->lz4_data);
		if (rv < 0)
			return (rv);
		rv = ADAPT_COMPRESS_LZ4;
		lz4_count++;

	} else if (adat->adapt_mode == 2 && PC_TYPE(btype) == TYPE_BINARY && !bsc_type) {
		rv = lzma_compress(src, srclen, dst, dstlen, level, chdr, btype, adat->lzma_data);
		if (rv < 0)
			return (rv);
		rv = ADAPT_COMPRESS_LZMA;
		lzma_count++;

	} else if (adat->adapt_mode == 1 && PC_TYPE(btype) == TYPE_BINARY && !bsc_type) {
		rv = bzip2_compress(src, srclen, dst, dstlen, level, chdr, btype, NULL);
		if (rv < 0)
			return (rv);
		rv = ADAPT_COMPRESS_BZIP2;
		bzip2_count++;

	} else {
#ifdef ENABLE_PC_LIBBSC
		if (adat->bsc_data && bsc_type) {
			rv = libbsc_compress(src, srclen, dst, dstlen, level, chdr, btype, adat->bsc_data);
			if (rv < 0)
				return (rv);
			rv = ADAPT_COMPRESS_BSC;
			bsc_count++;
		} else {
#endif
			rv = ppmd_alloc(adat->ppmd_data);
			if (rv < 0)
				return (rv);
			rv = ppmd_compress(src, srclen, dst, dstlen, level, chdr, btype, adat->ppmd_data);
			ppmd_free(adat->ppmd_data);
			if (rv < 0)
				return (rv);
			rv = ADAPT_COMPRESS_PPMD;
			ppmd_count++;
#ifdef ENABLE_PC_LIBBSC
		}
#endif
	}

	return (rv);
}

int
adapt_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data)
{
	struct adapt_data *adat = (struct adapt_data *)(data);
	uchar_t cmp_flags;

	cmp_flags = CHDR_ALGO(chdr);

	if (cmp_flags == ADAPT_COMPRESS_LZ4) {
		return (lz4_decompress(src, srclen, dst, dstlen, 1, chdr, btype, adat->lz4_data));

	} else if (cmp_flags == ADAPT_COMPRESS_LZMA) {
		return (lzma_decompress(src, srclen, dst, dstlen, level, chdr, btype, adat->lzma_data));

	} else if (cmp_flags == ADAPT_COMPRESS_BZIP2) {
		return (bzip2_decompress(src, srclen, dst, dstlen, level, chdr, btype, NULL));

	} else if (cmp_flags == ADAPT_COMPRESS_PPMD) {
		int rv;
		rv = ppmd_alloc(adat->ppmd_data);
		if (rv < 0)
			return (rv);
		rv = ppmd_decompress(src, srclen, dst, dstlen, level, chdr, btype, adat->ppmd_data);
		ppmd_free(adat->ppmd_data);
		return (rv);

	} else if (cmp_flags == ADAPT_COMPRESS_BSC) {
#ifdef ENABLE_PC_LIBBSC
		return (libbsc_decompress(src, srclen, dst, dstlen, level, chdr, btype, adat->bsc_data));
#else
		log_msg(LOG_ERR, 0, "Cannot decompress chunk. Libbsc support not present.\n");
		return (-1);
#endif

	} else {
		log_msg(LOG_ERR, 0, "Unrecognized compression mode: %d, file corrupt.\n", cmp_flags);
	}
	return (-1);
}
