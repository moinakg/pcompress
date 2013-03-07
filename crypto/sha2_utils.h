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

#ifndef _SHA2_UTILS_H_
#define	_SHA2_UTILS_H_

void ossl_SHA256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void ossl_SHA256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512t256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512t256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

void ossl_SHA512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void ossl_SHA512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

#endif

