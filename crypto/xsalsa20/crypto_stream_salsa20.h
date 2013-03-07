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

#ifndef crypto_stream_salsa20_H
#define crypto_stream_salsa20_H

#define crypto_stream_salsa20_amd64_xmm6_KEYBYTES 32
#define crypto_stream_salsa20_amd64_xmm6_NONCEBYTES 8
#ifdef __cplusplus
extern "C" {
#endif
extern int crypto_stream_salsa20_amd64_xmm6(unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
extern int crypto_stream_salsa20_amd64_xmm6_xor(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
extern int crypto_stream_salsa20_ref(unsigned char *c,unsigned long long clen, const unsigned char *n, const unsigned char *k);
extern int crypto_stream_salsa20_ref_xor(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
#ifdef __cplusplus
}
#endif

#ifndef SALSA20_DEBUG
#define crypto_stream_salsa20 crypto_stream_salsa20_amd64_xmm6
#define crypto_stream_salsa20_xor crypto_stream_salsa20_amd64_xmm6_xor
#else
#define crypto_stream_salsa20 crypto_stream_salsa20_ref
#define crypto_stream_salsa20_xor crypto_stream_salsa20_ref_xor
#endif
#define crypto_stream_salsa20_KEYBYTES crypto_stream_salsa20_amd64_xmm6_KEYBYTES
#define crypto_stream_salsa20_NONCEBYTES crypto_stream_salsa20_amd64_xmm6_NONCEBYTES

#endif
