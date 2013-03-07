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

#ifndef crypto_xsalsa20_H
#define crypto_xsalsa20_H

#include <inttypes.h>
#include <utils.h>

#define XSALSA20_CRYPTO_KEYBYTES 32
#define XSALSA20_CRYPTO_NONCEBYTES 24

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	unsigned char nonce[XSALSA20_CRYPTO_NONCEBYTES];
	uchar_t key[XSALSA20_CRYPTO_KEYBYTES];
	int keylen;
	uchar_t pkey[XSALSA20_CRYPTO_KEYBYTES];
} salsa20_ctx_t;

int salsa20_init(salsa20_ctx_t *ctx, uchar_t *salt, int saltlen, uchar_t *pwd, int pwd_len, uchar_t *nonce, int enc);
int salsa20_encrypt(salsa20_ctx_t *ctx, uchar_t *plaintext, uchar_t *ciphertext, uint64_t len, uint64_t id);
int salsa20_decrypt(salsa20_ctx_t *ctx, uchar_t *ciphertext, uchar_t *plaintext, uint64_t len, uint64_t id);
uchar_t *salsa20_nonce(salsa20_ctx_t *ctx);
void salsa20_clean_pkey(salsa20_ctx_t *ctx);
void salsa20_cleanup(salsa20_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
