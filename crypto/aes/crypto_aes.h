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
 * This program includes partly-modified public domain source
 * code from the LZMA SDK: http://www.7-zip.org/sdk.html
 */

#ifndef	_AES_CRYPTO_H
#define	_AES_CRYPTO_H

#include <utils.h>
#include <openssl/aes.h>
#ifdef	_USE_PBK
#include <openssl/evp.h>
#endif
#include <openssl/opensslv.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef KEYLEN
#define	KEYLEN 16
#endif
#define	PBE_ROUNDS 1000

typedef struct {
	uint64_t nonce;
	AES_KEY key;
	uchar_t pkey[KEYLEN];
} aes_ctx_t;

int aes_init(aes_ctx_t *ctx, uchar_t *salt, int saltlen, uchar_t *pwd, int pwd_len,
	     uint64_t nonce, int enc);
int aes_encrypt(aes_ctx_t *ctx, uchar_t *plaintext, uchar_t *ciphertext, uint64_t len, uint64_t id);
int aes_decrypt(aes_ctx_t *ctx, uchar_t *ciphertext, uchar_t *plaintext, uint64_t len, uint64_t id);
uint64_t aes_nonce(aes_ctx_t *ctx);
void aes_clean_pkey(aes_ctx_t *ctx);
void aes_cleanup(aes_ctx_t *ctx);

#ifdef	__cplusplus
}
#endif

#endif	
