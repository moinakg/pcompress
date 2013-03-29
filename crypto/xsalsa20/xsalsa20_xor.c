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

/*
version 20080913
D. J. Bernstein
Public domain.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <assert.h>
#include <crypto_scrypt.h>
#include "crypto_core_hsalsa20.h"
#include "crypto_stream_salsa20.h"
#include "crypto_xsalsa20.h"

extern int geturandom_bytes(uchar_t *rbytes, int nbytes);

static const unsigned char sigma[16] = "expand 32-byte k";
static const unsigned char tau[16] = "expand 16-byte k";

static int
crypto_xsalsa20(unsigned char *c, const unsigned char *m, unsigned long long mlen,
  const unsigned char *n, const unsigned char *k, int klen)
{
	unsigned char subkey[32];

	assert(klen == 32 || klen == 16);
	if (klen < XSALSA20_CRYPTO_KEYBYTES)
		crypto_core_hsalsa20(subkey,n,k,tau);
	else
		crypto_core_hsalsa20(subkey,n,k,sigma);
	return crypto_stream_salsa20_xor(c,m,mlen,n + 16,subkey);
}

int
salsa20_init(salsa20_ctx_t *ctx, uchar_t *salt, int saltlen, uchar_t *pwd, int pwd_len,
	 uchar_t *nonce, int enc)
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

	if (XSALSA20_CRYPTO_NONCEBYTES % 8) {
		fprintf(stderr, "XSALSA20_CRYPTO_NONCEBYTES is not a multiple of 8!\n");
		return (-1);
	}
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

	/*
	 * Copy the key. XSalsa20 core cipher always uses a 256-bit key. If we are using a
	 * 128-bit key then the key value is repeated twice to form a 256-bit value.
	 * This approach is based on the Salsa20 code submitted to eSTREAM. See the function
	 * ECRYPT_keysetup() in the Salsa20 submission:
	 * http://www.ecrypt.eu.org/stream/svn/viewcvs.cgi/ecrypt/trunk/submissions/salsa20/full/ref/salsa20.c?rev=161&view=auto
	 * 
	 * The input values corresponding to a 256-bit key contain repeated values if key
	 * length is 128-bit.
	 */
	memcpy(ctx->key, key, ctx->keylen);
	if (ctx->keylen < XSALSA20_CRYPTO_KEYBYTES) {
		uchar_t *k;
		k = ctx->key + ctx->keylen;
		memcpy(k, key, XSALSA20_CRYPTO_KEYBYTES - ctx->keylen);
	}

	if (enc) {
		int i;
		uint64_t *n, *n1;

		// Derive 192-bit nonce
		if (RAND_status() != 1 || RAND_bytes(IV, XSALSA20_CRYPTO_NONCEBYTES) != 1) {
			if (geturandom_bytes(IV, XSALSA20_CRYPTO_NONCEBYTES) != 0) {
				if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1) {
					time((time_t *)&tv);
				} else {
					tv = tp.tv_sec * 1000UL + tp.tv_nsec;
				}
				sprintf((char *)num, "%" PRIu64, tv);
				PKCS5_PBKDF2_HMAC((const char *)num, strlen((char *)num), salt,
						saltlen, PBE_ROUNDS, EVP_sha256(), 32, IV);
			}
		}
		n = (uint64_t *)IV;
		n1 = (uint64_t *)(ctx->nonce);
		for (i = 0; i < XSALSA20_CRYPTO_NONCEBYTES/8; i++) {
			*n1 = LE64(*n);
			n++;
			n1++;
		}

		// Nullify stack components
		memset(num, 0, 25);
		memset(IV, 0, 32);
		memset(&tp, 0, sizeof (tp));
		tv = 0;
	} else {
		memcpy(ctx->nonce, nonce, XSALSA20_CRYPTO_NONCEBYTES);
		memset(nonce, 0, XSALSA20_CRYPTO_NONCEBYTES);
	}
	return (0);
}

int
salsa20_encrypt(salsa20_ctx_t *ctx, uchar_t *plaintext, uchar_t *ciphertext, uint64_t len, uint64_t id)
{
	uchar_t nonce[XSALSA20_CRYPTO_NONCEBYTES];
	int i, rv;
	uint64_t *n, carry;

	for (i = 0; i < XSALSA20_CRYPTO_NONCEBYTES; i++) nonce[i] = ctx->nonce[i];
	carry = id;
	n = (uint64_t *)nonce;
	for (i = 0; i < XSALSA20_CRYPTO_NONCEBYTES/8; i++) {
		if (UINT64_MAX - *n < carry) {
			carry = carry - (UINT64_MAX - *n);
			*n = 0;
		} else {
			*n += carry;
			carry = 0;
			break;
		}
		++n;
	}
	if (carry) {
		n = (uint64_t *)nonce;
		*n += carry;
		carry = 0;
	}

	rv = crypto_xsalsa20(ciphertext, plaintext, len, nonce, ctx->key, ctx->keylen);
	n = (uint64_t *)nonce;
	for (i = 0; i < XSALSA20_CRYPTO_NONCEBYTES/8; i++) {
		*n = 0;
		++n;
	}
	return (rv);
}

int
salsa20_decrypt(salsa20_ctx_t *ctx, uchar_t *ciphertext, uchar_t *plaintext, uint64_t len, uint64_t id)
{
	uchar_t nonce[XSALSA20_CRYPTO_NONCEBYTES];
	int i, rv;
	uint64_t *n, carry;

	for (i = 0; i < XSALSA20_CRYPTO_NONCEBYTES; i++) nonce[i] = ctx->nonce[i];
	carry = id;
	n = (uint64_t *)nonce;
	for (i = 0; i < XSALSA20_CRYPTO_NONCEBYTES/8; i++) {
		if (UINT64_MAX - *n < carry) {
			carry = carry - (UINT64_MAX - *n);
			*n = 0;
		} else {
			*n += carry;
			carry = 0;
			break;
		}
		++n;
	}
	if (carry) {
		n = (uint64_t *)nonce;
		*n += carry;
		carry = 0;
	}

	rv = crypto_xsalsa20(plaintext, ciphertext, len, nonce, ctx->key, ctx->keylen);
	n = (uint64_t *)nonce;
	for (i = 0; i < XSALSA20_CRYPTO_NONCEBYTES/8; i++) {
		*n = 0;
		++n;
	}
	return (rv);
}

uchar_t *
salsa20_nonce(salsa20_ctx_t *ctx)
{
	return (ctx->nonce);
}

void
salsa20_clean_pkey(salsa20_ctx_t *ctx)
{
	memset(ctx->pkey, 0, ctx->keylen);
}

void
salsa20_cleanup(salsa20_ctx_t *ctx)
{
	memset((void *)(&ctx->key), 0, sizeof (ctx->key));
	memset(ctx->nonce, 0, XSALSA20_CRYPTO_NONCEBYTES);
	free(ctx);
}
