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
 */

#ifndef	_DELTA2_H
#define	_DELTA2_H

#include <arpa/nameser_compat.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

int delta2_encode(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen, int rle_thresh);
int delta2_decode(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen);

#define	ULL_MAX (18446744073709551615ULL)

#ifdef	__cplusplus
}
#endif

#endif	
