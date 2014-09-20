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

int
analyze_buffer(void *src, uint64_t srclen, int btype, int adapt_mode)
{
	uchar_t *src1 = (uchar_t *)src;
	int stype = PC_SUBTYPE(btype);

	if (btype == TYPE_UNKNOWN || stype == TYPE_ARCHIVE_TAR) {
		uint32_t freq[256], freq0x80[2] = {0};
		uint64_t i, alphabetNum = 0, tot8b = 0;
		uchar_t cur_byte;

                /*
                 * Count number of 8-bit binary bytes and XML tags in source.
                 */
                tot8b = 0;
		for (i = 0; i < srclen; i++) {
                        cur_byte = src1[i];
                        tot8b += (cur_byte & 0x80); // This way for possible auto-vectorization
			freq[cur_byte]++;
		}

		for (i = 0; i < 256; i++)
			freq0x80[i>>7]+=freq[i];

		for(i = 'a'; i <= 'z'; i++)
			alphabetNum+=freq[i];

                /*
                 * Heuristics for detecting BINARY vs generic TEXT
                 */
                tot8b /= 0x80;
		if (tot8b < (srclen>>2 + srclen>>3)) {
                        btype = TYPE_TEXT;
			if (freq0x80[1]<(srclen>>3) && (freq[' ']>(srclen>>7)) 
			    && (freq['a']+freq['e']+freq['t']>(srclen>>4))
			    && alphabetNum>(srclen>>2)) {
				btype |= TYPE_ENGLISH;
			}
		}
	}

	return (btype);
}
