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

#ifndef	_AES_CRYPTO_H
#define	_AES_CRYPTO_H

#include <utils.h>
#include <openssl/aes.h>
#ifdef	_USE_PBK
#include <openssl/evp.h>
#endif
#include <openssl/opensslv.h>
#include <crypto_utils.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	uint64_t nonce;
	AES_KEY key;
	int keylen;
	uchar_t pkey[MAX_KEYLEN];
} aes_ctx_t;

int aes_init(aes_ctx_t *ctx, uchar_t *salt, int saltlen, uchar_t *pwd, int pwd_len,
	     uint64_t nonce, int enc);
int aes_encrypt(aes_ctx_t *ctx, uchar_t *plaintext, uchar_t *ciphertext, uint64_t len, uint64_t id);
int aes_decrypt(aes_ctx_t *ctx, uchar_t *ciphertext, uchar_t *plaintext, uint64_t len, uint64_t id);
uchar_t *aes_nonce(aes_ctx_t *ctx);
void aes_clean_pkey(aes_ctx_t *ctx);
void aes_cleanup(aes_ctx_t *ctx);
void aes_module_init(processor_info_t *pc);

#ifdef	__cplusplus
}
#endif

#endif	
