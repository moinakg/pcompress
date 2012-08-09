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
 * This RLE encoder is a simple approach to encode long runs of '0'
 * bytes that typically are found in a bsdiff patch output. This
 * does not encode repeating runs of other characters.
 */

#include <utils.h>
#include <stdio.h>

#define ZERO_MASK (32768)
#define DATA_MASK (32767)
#define COUNT_MAX (32767)

int
zero_rle_encode(const void *const ibuf, const unsigned int ilen,
	void *obuf, unsigned int *const olen)
{
	unsigned int pos1, pos2;
	unsigned short count;
	const uchar_t *const ib = ibuf;
	uchar_t *ob = obuf;

	pos2 = 0;
	for (pos1=0; pos1<ilen && pos2<*olen;) {
		count = 0;
		if (ib[pos1] == 0) {
			for (;pos1<ilen && ib[pos1]==0 && count<COUNT_MAX; pos1++) count++;
			count |= ZERO_MASK;
			*((unsigned short *)(ob + pos2)) = htons(count);
			pos2 += 2;
		} else {
			unsigned int pos3, pos4, state;
			pos3 = pos2;
			pos2 += 2;
			if (pos2 > *olen) break;

			state = 0;
			for (;pos1<ilen && pos2<*olen && count<COUNT_MAX;) {
				if (ib[pos1] != 0) state = 0;
				if (ib[pos1] == 0 && !state) {
					state = 1;
					// Lookahead if there are at least 4 consecutive zeroes
					if (ilen > 3) {
						pos4 = *((unsigned int *)(ib+pos1));
						if (!pos4) break;
					}
				}
				ob[pos2++] = ib[pos1++];
				count++;
			}
			*((unsigned short *)(ob + pos3)) = htons(count);
		}
	}
	*olen = pos2;
	if (pos1 < ilen) {
		return (-1);
	} else {
		return (0);
	}
}

int
zero_rle_decode(const void* ibuf, unsigned int ilen,
	void* obuf, unsigned int *olen)
{
	unsigned int pos1, pos2, i;
	unsigned short count;
	const uchar_t *ib = ibuf;
	uchar_t *ob = obuf;

	pos2 = 0;
	pos1 = 0;
	for (; pos1<ilen && pos2<*olen;) {
		count = ntohs(*((unsigned short *)(ib + pos1)));
		pos1 += 2;
		if (count & ZERO_MASK) {
			count &= DATA_MASK;
			for (i=0; i<count && pos2<*olen; i++)
				ob[pos2++] = 0;
		} else {
			for (i=0; i<count && pos1<ilen && pos2<*olen; i++)
				ob[pos2++] = ib[pos1++];
		}
	}
	i = *olen;
	*olen = pos2;
	if (pos1 < ilen || pos2 < i) {
		return (-1);
	} else {
		return (0);
	}
}


