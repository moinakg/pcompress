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

/*-
 * Copyright 2007-2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */

#ifndef __STDC_FORMAT_MACROS
#define	__STDC_FORMAT_MACROS	1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <crypto_scrypt.h>
#include <crypto_aesctr.h>
#include <utils.h>
#include "crypto_aes.h"

extern int geturandom_bytes(uchar_t *rbytes, int nbytes);
extern uint64_t lzma_crc64(const uint8_t *buf, size_t size, uint64_t crc);
extern int vpaes_set_encrypt_key(const unsigned char *userKey, int bits, AES_KEY *key);
extern void vpaes_encrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key);
extern int aesni_set_encrypt_key(const unsigned char *userKey, int bits, AES_KEY *key);
extern void aesni_encrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key);

setkey_func_ptr enc_setkey;
encrypt_func_ptr enc_encrypt;

void
aes_module_init(processor_info_t *pc)
{
	enc_setkey = AES_set_encrypt_key;
	enc_encrypt = AES_encrypt;

	if (pc->proc_type == PROC_X64_INTEL || pc->proc_type == PROC_X64_AMD) {
		if (pc->aes_avail) {
			enc_setkey = aesni_set_encrypt_key;
			enc_encrypt = aesni_encrypt;

		} else if (pc->sse_level >= 3 && pc->sse_sub_level >= 1) {
			enc_setkey = vpaes_set_encrypt_key;
			enc_encrypt = vpaes_encrypt;
		}
	}
}

int
aes_init(aes_ctx_t *ctx, uchar_t *salt, int saltlen, uchar_t *pwd, int pwd_len,
	 uint64_t nonce, int enc)
{
	struct timespec tp;
	uint64_t tv;
	uchar_t num[25];
	uchar_t IV[32];
	uchar_t *key = ctx->pkey;

#ifndef	_USE_PBK
	int logN;
	uint32_t r, p;
	uint64_t N;

	pickparams(&logN, &r, &p);
	N = (uint64_t)(1) << logN;
	if (crypto_scrypt(pwd, pwd_len, salt, saltlen, N, r, p, key, ctx->keylen)) {
		fprintf(stderr, "Scrypt failed\n");
		return (-1);
	}
#else
	rv = PKCS5_PBKDF2_HMAC(pwd, pwd_len, salt, saltlen, PBE_ROUNDS, EVP_sha256(),
			       ctx->keylen, key);
	if (rv != ctx->keylen) {
		fprintf(stderr, "Key size is %d bytes - should be %d bits\n", i, ctx->keylen);
		return (-1);
	}
#endif

	if (enc) {
		enc_setkey(key, (ctx->keylen << 3), &(ctx->key));
		// Derive 64-bit nonce
		if (RAND_status() != 1 || RAND_bytes((uchar_t *)&(ctx->nonce), 8) != 1) {
			if (geturandom_bytes((uchar_t *)&(ctx->nonce), 8) != 0) {
				if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1) {
					time((time_t *)&tv);
				} else {
					tv = tp.tv_sec * 1000UL + tp.tv_nsec;
				}
				sprintf((char *)num, "%" PRIu64, tv);
				PKCS5_PBKDF2_HMAC((const char *)num, strlen((char *)num), salt,
						saltlen, PBE_ROUNDS, EVP_sha256(), 32, IV);
				ctx->nonce = lzma_crc64(IV, 32, 0);
			} else {
				tv = ntohll(ctx->nonce);
				ctx->nonce = tv;
			}
		} else {
			/*
			 * Random bytes are treated as being in big-endian format.
			 */
			tv = ntohll(ctx->nonce);
			ctx->nonce = tv;
		}
		// Nullify stack components
		memset(num, 0, 25);
		memset(IV, 0, 32);
		memset(&tp, 0, sizeof (tp));
		tv = 0;
	} else {
		ctx->nonce = nonce;
		enc_setkey(key, (ctx->keylen << 3), &(ctx->key));
	}
	return (0);
}

int
aes_encrypt(aes_ctx_t *ctx, uchar_t *plaintext, uchar_t *ciphertext, uint64_t len, uint64_t id) {
	AES_KEY key;
	uchar_t *k1, *k2;
	struct crypto_aesctr *strm;
	int i;

	k1 = (uchar_t *)&(ctx->key);
	k2 = (uchar_t *)&key;
	for (i=0; i<sizeof (key); i++)
		k2[i] = k1[i];

	// Init counter mode AES from scrypt
	strm = crypto_aesctr_init(&key, ctx->nonce + id);
	if (!strm) {
		fprintf(stderr, "Failed to init counter mode AES\n");
		return (-1);
	}
	crypto_aesctr_stream(strm, plaintext, ciphertext, len);
	crypto_aesctr_free(strm);
	strm = NULL;
	k1 = NULL;
	k2 = NULL;
	memset(&key, 0, sizeof (key));
	return (0);
}

int
aes_decrypt(aes_ctx_t *ctx, uchar_t *ciphertext, uchar_t *plaintext, uint64_t len, uint64_t id) {
	AES_KEY key;
	uchar_t *k1, *k2;
	struct crypto_aesctr *strm;
	int i;

	k1 = (uchar_t *)&(ctx->key);
	k2 = (uchar_t *)&key;
	for (i=0; i<sizeof (key); i++)
		k2[i] = k1[i];

	// Init counter mode AES from scrypt
	strm = crypto_aesctr_init(&key, ctx->nonce + id);
	if (!strm) {
		fprintf(stderr, "Failed to init counter mode AES\n");
		return (-1);
	}
	crypto_aesctr_stream(strm, ciphertext, plaintext, len);
	crypto_aesctr_free(strm);
	strm = NULL;
	k1 = NULL;
	k2 = NULL;
	memset(&key, 0, sizeof (key));
	return (0);
}

uchar_t *
aes_nonce(aes_ctx_t *ctx)
{
	return ((uchar_t *)&(ctx->nonce));
}

void
aes_clean_pkey(aes_ctx_t *ctx)
{
	memset(ctx->pkey, 0, ctx->keylen);
}

void
aes_cleanup(aes_ctx_t *ctx)
{
	memset((void *)(&ctx->key), 0, sizeof (ctx->key));
	ctx->nonce = 0;
	free(ctx);
}
