/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2014 Moinak Ghosh. All rights reserved.
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

#include "utils.h"
#include "analyzer.h"

#define	FIFTY_PCT(x)	((((double)x)/10) * 5)
#define	THIRTY_PCT(x)	((((double)x)/10) * 3)
#define	TEN_PCT(x)	(((double)x)/10)

void
analyze_buffer(void *src, uint64_t srclen, analyzer_ctx_t *actx)
{
	uchar_t *src1 = (uchar_t *)src;
	uint64_t i, tot8b, tot_8b, lbytes, spc;
	uchar_t cur_byte, prev_byte;
	uint64_t tag1, tag2, tag3;
	double tagcnt, pct_tag;

	/*
	 * Count number of 8-bit binary bytes and XML tags in source.
	 */
	tot8b = 0;
	tag1 = 0;
	tag2 = 0;
	tag3 = 0;
	lbytes = 0;
	spc = 0;
	prev_byte = cur_byte = 0;
	memset(actx, 0, sizeof (analyzer_ctx_t));
	for (i = 0; i < srclen; i++) {
		cur_byte = src1[i];
		tot8b += (cur_byte > 127);
		lbytes += (cur_byte < 32);
		spc += (cur_byte == ' ');
		tag1 += (cur_byte == '<');
		tag2 += (cur_byte == '>');
		tag3 += ((prev_byte == '<') & (cur_byte == '/'));
		tag3 += ((prev_byte == '/') & (cur_byte == '>'));
		if (cur_byte != ' ')
			prev_byte = cur_byte;
	}

	/*
	 * Heuristics for detecting BINARY vs generic TEXT vs XML data at various
	 * significance levels.
	 */
	tot_8b = tot8b + lbytes;
	tagcnt = tag1 + tag2;
	pct_tag = tagcnt / (double)srclen;
	if (tot_8b > THIRTY_PCT(srclen)) {
		actx->thirty_pct.btype = TYPE_BINARY;
	} else {
		actx->thirty_pct.btype = TYPE_TEXT;
	}

	if (tot_8b > FIFTY_PCT(srclen)) {
		actx->fifty_pct.btype = TYPE_BINARY;
	} else {
		actx->fifty_pct.btype = TYPE_TEXT;
	}

	/* This should be tot8b and not tot_8b. */
	if (tot8b <= TEN_PCT((double)srclen) && lbytes < ((srclen>>1) + (srclen>>2) + (srclen>>3))) {
		actx->ten_pct.btype = TYPE_TEXT;
	} else {
		actx->ten_pct.btype = TYPE_BINARY;
	}

	if (tag1 > tag2 - 4 && tag1 < tag2 + 4 && tag3 > (double)tag1 * 0.40 &&
	    tagcnt > (double)spc * 0.06) {
		actx->thirty_pct.btype |= TYPE_MARKUP;
		actx->fifty_pct.btype |= TYPE_MARKUP;
		actx->ten_pct.btype |= TYPE_MARKUP;
	}
}

int
analyze_buffer_simple(void *src, uint64_t srclen)
{
	uchar_t *src1 = (uchar_t *)src;
	uint64_t i, tot8b, lbytes;
	uchar_t cur_byte;
	int btype = TYPE_UNKNOWN;
	/*
	 * Count number of 8-bit binary bytes in source
	 */
	tot8b = 0;
	lbytes = 0;
	for (i = 0; i < srclen; i++) {
		cur_byte = src1[i];
		tot8b += (cur_byte & 0x80); // This way for possible auto-vectorization
		lbytes += (cur_byte < 32);
	}
	/*
	 * Heuristics for detecting BINARY vs generic TEXT
	 */
	tot8b /= 0x80;
	if (tot8b <= TEN_PCT((double)srclen) && lbytes < ((srclen>>1) + (srclen>>2) + (srclen>>3))) {
		btype = TYPE_TEXT;
	}
	return (btype);
}

