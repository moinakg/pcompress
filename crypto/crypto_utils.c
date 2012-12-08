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

#include <sys/types.h>
#include <sys/param.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <skein.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sha256.h>
#include <crypto_aes.h>
#include <KeccakNISTInterface.h>

#include "crypto_utils.h"
#include "cpuid.h"

#define	PROVIDER_OPENSSL	0
#define	PROVIDER_X64_OPT	1

static void init_sha256(void);
static int geturandom_bytes(uchar_t rbytes[32]);
/*
 * Checksum properties
 */
typedef void (*ckinit_func_ptr)(void);
static struct {
	char	*name;
	cksum_t	cksum_id;
	int	bytes, mac_bytes;
	ckinit_func_ptr init_func;
} cksum_props[] = {
	{"CRC64",	CKSUM_CRC64,		8,	32,	NULL},
	{"SKEIN256",	CKSUM_SKEIN256,		32,	32,	NULL},
	{"SKEIN512",	CKSUM_SKEIN512,		64,	64,	NULL},
	{"SHA256",	CKSUM_SHA256,		32,	32,	init_sha256},
	{"SHA512",	CKSUM_SHA512,		64,	64,	NULL},
	{"KECCAK256",	CKSUM_KECCAK256,	32,	32,	NULL},
	{"KECCAK512",	CKSUM_KECCAK512,	64,	64,	NULL}
};

static int cksum_provider = PROVIDER_OPENSSL, ossl_inited = 0;

extern uint64_t lzma_crc64(const uint8_t *buf, size_t size, uint64_t crc);
extern uint64_t lzma_crc64_8bchk(const uint8_t *buf, size_t size,
	uint64_t crc, uint64_t *cnt);

int
compute_checksum(uchar_t *cksum_buf, int cksum, uchar_t *buf, ssize_t bytes)
{
	if (cksum == CKSUM_CRC64) {
		uint64_t *ck = (uint64_t *)cksum_buf;
		*ck = lzma_crc64(buf, bytes, 0);

	} else if (cksum == CKSUM_SKEIN256) {
		Skein_512_Ctxt_t ctx;

		Skein_512_Init(&ctx, 256);
		Skein_512_Update(&ctx, buf, bytes);
		Skein_512_Final(&ctx, cksum_buf);

	} else if (cksum == CKSUM_SKEIN512) {
		Skein_512_Ctxt_t ctx;

		Skein_512_Init(&ctx, 512);
		Skein_512_Update(&ctx, buf, bytes);
		Skein_512_Final(&ctx, cksum_buf);

	} else if (cksum == CKSUM_SHA256) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			SHA256_CTX ctx;

			SHA256_Init(&ctx);
			SHA256_Update(&ctx, buf, bytes);
			SHA256_Final(cksum_buf, &ctx);
		} else {
			SHA256_Context ctx;

			opt_SHA256_Init(&ctx);
			opt_SHA256_Update(&ctx, buf, bytes);
			opt_SHA256_Final(&ctx, cksum_buf);
		}
	} else if (cksum == CKSUM_SHA512) {
		SHA512_CTX ctx;

		SHA512_Init(&ctx);
		SHA512_Update(&ctx, buf, bytes);
		SHA512_Final(cksum_buf, &ctx);

	} else if (cksum == CKSUM_KECCAK256) {
		if (Keccak_Hash(256, buf, bytes, cksum_buf) != 0)
			return (-1);

	} else if (cksum == CKSUM_KECCAK512) {
		if (Keccak_Hash(512, buf, bytes, cksum_buf) != 0)
			return (-1);
	} else {
		return (-1);
	}
	return (0);
}

static void
init_sha256(void)
{
#ifdef	WORDS_BIGENDIAN
	cksum_provider = PROVIDER_OPENSSL;
#else
#ifdef	__x86_64__
	processor_info_t pc;

	cksum_provider = PROVIDER_OPENSSL;
	cpuid_basic_identify(&pc);
	if (pc.proc_type == PROC_X64_INTEL || pc.proc_type == PROC_X64_AMD) {
		if (opt_Init_SHA(&pc) == 0) {
			cksum_provider = PROVIDER_X64_OPT;
		}
	}
#endif
#endif
}

/*
 * Check if either the given checksum name or id is valid and
 * return it's properties.
 */
int
get_checksum_props(char *name, int *cksum, int *cksum_bytes, int *mac_bytes)
{
	int i;

	for (i=0; i<sizeof (cksum_props); i++) {
		if ((name != NULL && strcmp(name, cksum_props[i].name) == 0) ||
		    (*cksum != 0 && *cksum == cksum_props[i].cksum_id)) {
			*cksum = cksum_props[i].cksum_id;
			*cksum_bytes = cksum_props[i].bytes;
			*mac_bytes = cksum_props[i].mac_bytes;
			if (cksum_props[i].init_func)
				cksum_props[i].init_func();
			return (0);
		}
	}
	return (-1);
}

/*
 * Endian independent way of storing the checksum bytes. This is actually
 * storing in little endian format and a copy can be avoided in x86 land.
 * However unsightly ifdefs are avoided here since this is not so performance
 * critical.
 */
void
serialize_checksum(uchar_t *checksum, uchar_t *buf, int cksum_bytes)
{
	int i,j;

	j = 0;
	for (i=cksum_bytes; i>0; i--) {
		buf[j] = checksum[i-1];
		j++;
	}
}

void
deserialize_checksum(uchar_t *checksum, uchar_t *buf, int cksum_bytes)
{
	int i,j;

	j = 0;
	for (i=cksum_bytes; i>0; i--) {
		checksum[i-1] = buf[j];
		j++;
	}
}

/*
 * Perform keyed hashing. With Skein, HMAC is not used, rather Skein's
 * native MAC is used which is more optimal than HMAC.
 */
int
hmac_init(mac_ctx_t *mctx, int cksum, crypto_ctx_t *cctx)
{
	aes_ctx_t *actx = (aes_ctx_t *)(cctx->crypto_ctx);
	mctx->mac_cksum = cksum;

	if (cksum == CKSUM_SKEIN256) {
		Skein_512_Ctxt_t *ctx = malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) return (-1);
		Skein_512_InitExt(ctx, 256, SKEIN_CFG_TREE_INFO_SEQUENTIAL,
				 actx->pkey, KEYLEN);
		mctx->mac_ctx = ctx;
		ctx = malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		memcpy(ctx, mctx->mac_ctx, sizeof (Skein_512_Ctxt_t));
		mctx->mac_ctx_reinit = ctx;

	} else if (cksum == CKSUM_SKEIN512) {
		Skein_512_Ctxt_t *ctx = malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) return (-1);
		Skein_512_InitExt(ctx, 512, SKEIN_CFG_TREE_INFO_SEQUENTIAL,
				  actx->pkey, KEYLEN);
		mctx->mac_ctx = ctx;
		ctx = malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		memcpy(ctx, mctx->mac_ctx, sizeof (Skein_512_Ctxt_t));
		mctx->mac_ctx_reinit = ctx;

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_CTX *ctx = malloc(sizeof (HMAC_CTX));
			if (!ctx) return (-1);
			HMAC_CTX_init(ctx);
			HMAC_Init_ex(ctx, actx->pkey, KEYLEN, EVP_sha256(), NULL);
			mctx->mac_ctx = ctx;

			ctx = malloc(sizeof (HMAC_CTX));
			if (!ctx) {
				free(mctx->mac_ctx);
				return (-1);
			}
			if (!HMAC_CTX_copy(ctx, mctx->mac_ctx)) {
				free(ctx);
				free(mctx->mac_ctx);
				return (-1);
			}
			mctx->mac_ctx_reinit = ctx;
		} else {
			HMAC_SHA256_Context *ctx = malloc(sizeof (HMAC_SHA256_Context));
			if (!ctx) return (-1);
			opt_HMAC_SHA256_Init(ctx, actx->pkey, KEYLEN);
			mctx->mac_ctx = ctx;

			ctx = malloc(sizeof (HMAC_SHA256_Context));
			if (!ctx) {
				free(mctx->mac_ctx);
				return (-1);
			}
			memcpy(ctx, mctx->mac_ctx, sizeof (HMAC_SHA256_Context));
			mctx->mac_ctx_reinit = ctx;
		}
	} else if (cksum == CKSUM_SHA512) {
		HMAC_CTX *ctx = malloc(sizeof (HMAC_CTX));
		if (!ctx) return (-1);
		HMAC_CTX_init(ctx);
		HMAC_Init_ex(ctx, actx->pkey, KEYLEN, EVP_sha512(), NULL);
		mctx->mac_ctx = ctx;

		ctx = malloc(sizeof (HMAC_CTX));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		if (!HMAC_CTX_copy(ctx, mctx->mac_ctx)) {
			free(ctx);
			free(mctx->mac_ctx);
			return (-1);
		}
		mctx->mac_ctx_reinit = ctx;

	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		hashState *ctx = malloc(sizeof (hashState));
		if (!ctx) return (-1);

		if (cksum == CKSUM_KECCAK256) {
			if (Keccak_Init(ctx, 256) != 0)
				return (-1);
		} else {
			if (Keccak_Init(ctx, 512) != 0)
				return (-1);
		}
		if (Keccak_Update(ctx, actx->pkey, KEYLEN << 3) != 0)
			return (-1);
		mctx->mac_ctx = ctx;

		ctx = malloc(sizeof (hashState));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		memcpy(ctx, mctx->mac_ctx, sizeof (hashState));
		mctx->mac_ctx_reinit = ctx;
	} else {
		return (-1);
	}
	return (0);
}

int
hmac_reinit(mac_ctx_t *mctx)
{
	int cksum = mctx->mac_cksum;

	if (cksum == CKSUM_SKEIN256 || cksum == CKSUM_SKEIN512) {
		memcpy(mctx->mac_ctx, mctx->mac_ctx_reinit, sizeof (Skein_512_Ctxt_t));

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_CTX_copy(mctx->mac_ctx, mctx->mac_ctx_reinit);
		} else {
			memcpy(mctx->mac_ctx, mctx->mac_ctx_reinit, sizeof (HMAC_SHA256_Context));
		}
	} else if (cksum == CKSUM_SHA512) {
		HMAC_CTX_copy(mctx->mac_ctx, mctx->mac_ctx_reinit);

	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		memcpy(mctx->mac_ctx, mctx->mac_ctx_reinit, sizeof (hashState));
	} else {
		return (-1);
	}
	return (0);
}

int
hmac_update(mac_ctx_t *mctx, uchar_t *data, size_t len)
{
	int cksum = mctx->mac_cksum;

	if (cksum == CKSUM_SKEIN256 || cksum == CKSUM_SKEIN512) {
		Skein_512_Update(mctx->mac_ctx, data, len);

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			if (HMAC_Update(mctx->mac_ctx, data, len) == 0)
				return (-1);
		} else {
			opt_HMAC_SHA256_Update(mctx->mac_ctx, data, len);
		}
	} else if (cksum == CKSUM_SHA512) {
		if (HMAC_Update(mctx->mac_ctx, data, len) == 0)
			return (-1);

	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		// Keccak takes data length in bits so we have to scale
		while (len > KECCAK_MAX_SEG) {
			uint64_t blen;

			blen = KECCAK_MAX_SEG;
			if (Keccak_Update(mctx->mac_ctx, data, blen << 3) != 0)
				return (-1);
			len -= KECCAK_MAX_SEG;
		}
		if (Keccak_Update(mctx->mac_ctx, data, len << 3) != 0)
			return (-1);
	} else {
		return (-1);
	}
	return (0);
}

int
hmac_final(mac_ctx_t *mctx, uchar_t *hash, unsigned int *len)
{
	int cksum = mctx->mac_cksum;

	if (cksum == CKSUM_SKEIN256) {
		Skein_512_Final(mctx->mac_ctx, hash);
		*len = 32;

	} else if (cksum == CKSUM_SKEIN512) {
		Skein_512_Final(mctx->mac_ctx, hash);
		*len = 64;

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_Final(mctx->mac_ctx, hash, len);
		} else {
			opt_HMAC_SHA256_Final(mctx->mac_ctx, hash);
			*len = 32;
		}
	} else if (cksum == CKSUM_SHA512) {
		HMAC_Final(mctx->mac_ctx, hash, len);

	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		if (Keccak_Final(mctx->mac_ctx, hash) != 0)
			return (-1);
		if (cksum == CKSUM_KECCAK256)
			*len = 32;
		else
			*len = 64;
	} else {
		return (-1);
	}
	return (0);
}

int
hmac_cleanup(mac_ctx_t *mctx)
{
	int cksum = mctx->mac_cksum;

	if (cksum == CKSUM_SKEIN256 || cksum == CKSUM_SKEIN512) {
		memset(mctx->mac_ctx, 0, sizeof (Skein_512_Ctxt_t));
		memset(mctx->mac_ctx_reinit, 0, sizeof (Skein_512_Ctxt_t));

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_CTX_cleanup(mctx->mac_ctx);
			HMAC_CTX_cleanup(mctx->mac_ctx_reinit);
		} else {
			memset(mctx->mac_ctx, 0, sizeof (HMAC_SHA256_Context));
			memset(mctx->mac_ctx_reinit, 0, sizeof (HMAC_SHA256_Context));
		}
	} else if (cksum == CKSUM_SHA512) {
		HMAC_CTX_cleanup(mctx->mac_ctx);
		HMAC_CTX_cleanup(mctx->mac_ctx_reinit);

	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		memset(mctx->mac_ctx, 0, sizeof (hashState));
		memset(mctx->mac_ctx_reinit, 0, sizeof (hashState));
	} else {
		return (-1);
	}
	mctx->mac_cksum = 0;
	free(mctx->mac_ctx);
	free(mctx->mac_ctx_reinit);
	return (0);
}

int
init_crypto(crypto_ctx_t *cctx, uchar_t *pwd, int pwd_len, int crypto_alg,
	    uchar_t *salt, int saltlen, uint64_t nonce, int enc_dec)
{
	if (crypto_alg == CRYPTO_ALG_AES) {
		aes_ctx_t *actx = malloc(sizeof (aes_ctx_t));

		if (enc_dec) {
			/*
			 * Encryption init.
			 */
			cctx->salt = malloc(32);
			salt = cctx->salt;
			cctx->saltlen = 32;
			if (RAND_status() != 1 || RAND_bytes(salt, 32) != 1) {
				if (geturandom_bytes(salt) != 0) {
					uchar_t sb[64];
					int b;
					struct timespec tp;

					b = 0;
					/* No good random pool is populated/available. What to do ? */
					if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1) {
						time((time_t *)&sb[b]);
						b += 8;
					} else {
						uint64_t v;
						v = tp.tv_sec * 1000UL + tp.tv_nsec;
						*((uint64_t *)&sb[b]) = v;
						b += 8;
					}
					*((uint32_t *)&sb[b]) = rand();
					b += 4;
					*((uint32_t *)&sb[b]) = getpid();
					b += 4;
					compute_checksum(&sb[b], CKSUM_SHA256, sb, b);
					b = 8 + 4;
					*((uint32_t *)&sb[b]) = rand();
					compute_checksum(salt, CKSUM_SHA256, &sb[b], 32 + 4);
				}
			}

			/*
			 * Zero nonce (arg #6) since it will be generated.
			 */
			if (aes_init(actx, salt, 32, pwd, pwd_len, 0, enc_dec) != 0) {
				fprintf(stderr, "Failed to initialize AES context\n");
				return (-1);
			}
		} else {
			/*
			 * Decryption init.
			 * Pass given nonce and salt.
			 */
			if (saltlen > MAX_SALTLEN) {
				fprintf(stderr, "Salt too long. Max allowed length is %d\n",
				    MAX_SALTLEN);
				return (-1);
			}
			cctx->salt = malloc(saltlen);
			memcpy(cctx->salt, salt, saltlen);

			if (aes_init(actx, cctx->salt, saltlen, pwd, pwd_len, nonce,
			    enc_dec) != 0) {
				fprintf(stderr, "Failed to initialize AES context\n");
				return (-1);
			}
		}
		cctx->crypto_ctx = actx;
		cctx->crypto_alg = crypto_alg;
		cctx->enc_dec = enc_dec;
	} else {
		fprintf(stderr, "Unrecognized algorithm code: %d\n", crypto_alg);
		return (-1);
	}
	return (0);
}

int
crypto_buf(crypto_ctx_t *cctx, uchar_t *from, uchar_t *to, ssize_t bytes, uint64_t id)
{
	if (cctx->crypto_alg == CRYPTO_ALG_AES) {
		if (cctx->enc_dec == ENCRYPT_FLAG) {
			return (aes_encrypt(cctx->crypto_ctx, from, to, bytes, id));
		} else {
			return (aes_decrypt(cctx->crypto_ctx, from, to, bytes, id));
		}
	} else {
		fprintf(stderr, "Unrecognized algorithm code: %d\n", cctx->crypto_alg);
		return (-1);
	}
	return (0);
}

uint64_t
crypto_nonce(crypto_ctx_t *cctx)
{
	return (aes_nonce(cctx->crypto_ctx));
}

void
crypto_clean_pkey(crypto_ctx_t *cctx)
{
	aes_clean_pkey(cctx->crypto_ctx);
}

void
cleanup_crypto(crypto_ctx_t *cctx)
{
	aes_cleanup(cctx->crypto_ctx);
	memset(cctx->salt, 0, 32);
	free(cctx->salt);
	free(cctx);
}

static int
geturandom_bytes(uchar_t rbytes[32])
{
	int fd;
	ssize_t lenread;
	uchar_t * buf = rbytes;
	size_t buflen = 32;

	/* Open /dev/urandom. */
	if ((fd = open("/dev/urandom", O_RDONLY)) == -1)
		goto err0;
	
	/* Read bytes until we have filled the buffer. */
	while (buflen > 0) {
		if ((lenread = read(fd, buf, buflen)) == -1)
			goto err1;
		
		/* The random device should never EOF. */
		if (lenread == 0)
			goto err1;
		
		/* We're partly done. */
		buf += lenread;
		buflen -= lenread;
	}
	
	/* Close the device. */
	while (close(fd) == -1) {
		if (errno != EINTR)
			goto err0;
	}
	
	/* Success! */
	return (0);
err1:
	close(fd);
err0:
	/* Failure! */
	return (4);
}

int
get_pw_string(char pw[MAX_PW_LEN], char *prompt, int twice)
{
	int fd, len;
	FILE *input, *strm;
	struct termios oldt, newt;
	uchar_t pw1[MAX_PW_LEN], pw2[MAX_PW_LEN], *s;

	// Try TTY first
	fd = open("/dev/tty", O_RDWR | O_NOCTTY);
	if (fd != -1) {
		input = fdopen(fd, "w+");
		strm = input;
	} else {
		// Fall back to stdin
		fd = STDIN_FILENO;
		input = stdin;
		strm = stderr;
	}
	tcgetattr(fd, &oldt);
	newt = oldt;
	newt.c_lflag &= ~ECHO;
	tcsetattr(fd, TCSANOW, &newt);

	fprintf(stderr, "%s: ", prompt);
	fflush(stderr);
	s = fgets(pw1, MAX_PW_LEN, input);
	fputs("\n", stderr);

	if (s == NULL) {
		tcsetattr(fd, TCSANOW, &oldt);
		fflush(strm);
		return (-1);
	}

	if (twice) {
		fprintf(stderr, "%s (once more): ", prompt);
		fflush(stderr);
		s = fgets(pw2, MAX_PW_LEN, input);
		tcsetattr(fd, TCSANOW, &oldt);
		fflush(strm);
		fputs("\n", stderr);

		if (s == NULL) {
			return (-1);
		}

		if (strcmp(pw1, pw2) != 0) {
			fprintf(stderr, "Passwords do not match!\n");
			memset(pw1, 0, MAX_PW_LEN);
			memset(pw2, 0, MAX_PW_LEN);
			return (-1);
		}
	} else {
		tcsetattr(fd, TCSANOW, &oldt);
		fflush(strm);
		fputs("\n", stderr);
	}

	len = strlen(pw1);
	pw1[len-1] = '\0';
	strcpy(pw, pw1);
	memset(pw1, 0, MAX_PW_LEN);
	memset(pw2, 0, MAX_PW_LEN);
	return (len);
}
