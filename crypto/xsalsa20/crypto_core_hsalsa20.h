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

#ifndef crypto_core_hsalsa20_H
#define crypto_core_hsalsa20_H

#define HSALSA_CRYPTO_OUTPUTBYTES 32
#define HSALSA_CRYPTO_INPUTBYTES 16
#define HSALSA_CRYPTO_CONSTBYTES 16

#ifdef __cplusplus
extern "C" {
#endif
int crypto_core_hsalsa20(unsigned char *out, const unsigned char *in, const unsigned char *k, const unsigned char *c);
#ifdef __cplusplus
}
#endif

#endif
