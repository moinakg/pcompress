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
#include <sha512.h>
#include <blake2_digest.h>
#include <crypto_aes.h>
#include <KeccakNISTInterface.h>
#include <utils.h>
#include <crypto_xsalsa20.h>

#include "crypto_utils.h"
#include "sha2_utils.h"
#include "sha3_utils.h"

#ifdef __HASH_COMPATIBILITY_
#include "old/sha2_utils_old.h"
#include "old/sha3_utils_old.h"
#endif

#define	PROVIDER_OPENSSL	0
#define	PROVIDER_X64_OPT	1

static void init_sha512(void);
static void init_blake2(void);
static struct blake2_dispatch	bdsp;

/*
 * Checksum properties
 */
typedef void (*ckinit_func_ptr)(void);
static struct {
	const char	*name;
	const char	*desc;
	cksum_t	cksum_id;
	int	bytes, mac_bytes;
	ckinit_func_ptr init_func;
	int		compatible;
} cksum_props[] = {
	{"CRC64",	"Extremely Fast 64-bit CRC from LZMA SDK.",
			CKSUM_CRC64,		8,	32,	NULL, 0},
	{"SKEIN256",	"256-bit SKEIN a NIST SHA3 runners-up (90% faster than Keccak).",
			CKSUM_SKEIN256,		32,	32,	NULL, 1},
	{"SKEIN512",	"512-bit SKEIN",
			CKSUM_SKEIN512,		64,	64,	NULL, 1},
	{"SHA256",	"SHA512/256 version of Intel's optimized (SSE,AVX) SHA2 for x86.",
			CKSUM_SHA256,		32,	32,	init_sha512, 0},
	{"SHA512",	"SHA512 version of Intel's optimized (SSE,AVX) SHA2 for x86.",
			CKSUM_SHA512,		64,	64,	init_sha512, 0},
	{"KECCAK256",	"Official 256-bit NIST SHA3 optimized implementation.",
			CKSUM_KECCAK256,		32,	32,	NULL, 0},
	{"KECCAK512",	"Official 512-bit NIST SHA3 optimized implementation.",
			CKSUM_KECCAK512,		64,	64,	NULL, 0},
	{"BLAKE256",	"Very fast 256-bit BLAKE2, derived from the NIST SHA3 runner-up BLAKE.",
			CKSUM_BLAKE256,		32,	32,	init_blake2, 0},
	{"BLAKE512",	"Very fast 256-bit BLAKE2, derived from the NIST SHA3 runner-up BLAKE.",
			CKSUM_BLAKE512,		64,	64,	init_blake2, 0}
};

static int cksum_provider = PROVIDER_OPENSSL;

extern uint64_t lzma_crc64(const uint8_t *buf, uint64_t size, uint64_t crc);
extern uint64_t lzma_crc64_8bchk(const uint8_t *buf, uint64_t size,
	uint64_t crc, uint64_t *cnt);

#ifdef __OSSL_OLD__
/*
 * The two functions below fill missing functionality in older versions of OpenSSL.
 */
int
HMAC_CTX_copy(HMAC_CTX *dctx, HMAC_CTX *sctx)
{
	if (!EVP_MD_CTX_copy(&dctx->i_ctx, &sctx->i_ctx))
		return (0);
	if (!EVP_MD_CTX_copy(&dctx->o_ctx, &sctx->o_ctx))
		return (0);
	if (!EVP_MD_CTX_copy(&dctx->md_ctx, &sctx->md_ctx))
		return (0);

	memcpy(dctx->key, sctx->key, HMAC_MAX_MD_CBLOCK);
	dctx->key_length = sctx->key_length;
	dctx->md = sctx->md;
	return (1);
}

int
PKCS5_PBKDF2_HMAC(const char *pass, int passlen,
	const unsigned char *salt, int saltlen, int iter,
	const EVP_MD *digest,
	int keylen, unsigned char *out)
{
	unsigned char digtmp[EVP_MAX_MD_SIZE], *p, itmp[4];
	int cplen, j, k, tkeylen, mdlen;
	unsigned long i = 1;
	HMAC_CTX hctx;

	mdlen = EVP_MD_size(digest);
	if (mdlen < 0)
		return 0;

	HMAC_CTX_init(&hctx);
	p = out;
	tkeylen = keylen;
	if(!pass)
		passlen = 0;
	else if(passlen == -1)
		passlen = strlen(pass);
	while(tkeylen)
	{
		if(tkeylen > mdlen)
			cplen = mdlen;
		else
			cplen = tkeylen;
		/* We are unlikely to ever use more than 256 blocks (5120 bits!)
		 * but just in case...
		 */
		itmp[0] = (unsigned char)((i >> 24) & 0xff);
		itmp[1] = (unsigned char)((i >> 16) & 0xff);
		itmp[2] = (unsigned char)((i >> 8) & 0xff);
		itmp[3] = (unsigned char)(i & 0xff);
		HMAC_Init_ex(&hctx, pass, passlen, digest, NULL);
		HMAC_Update(&hctx, salt, saltlen);
		HMAC_Update(&hctx, itmp, 4);
		HMAC_Final(&hctx, digtmp, NULL);
		memcpy(p, digtmp, cplen);
		for(j = 1; j < iter; j++)
		{
			HMAC(digest, pass, passlen,
			     digtmp, mdlen, digtmp, NULL);
			for(k = 0; k < cplen; k++)
				p[k] ^= digtmp[k];
		}
		tkeylen-= cplen;
		++i;
		p+= cplen;
	}
	HMAC_CTX_cleanup(&hctx);
	return (1);
}
#endif

int
get_crypto_alg(char *name)
{
	if (name[0] == 0 || name[1] == 0 || name[2] == 0) {
		return (0);
	}
	if (strncmp(name, "AES", 3) == 0) {
		return (CRYPTO_ALG_AES);
	} else {
		if (name[3] == 0 || name[4] == 0 || name[5] == 0 || name[6] == 0) {
			return (0);
		}
		if (strncmp(name, "SALSA20", 3) == 0) {
			return (CRYPTO_ALG_SALSA20);
		}
	}
	return (0);
}

/*
 * Compute a digest of the given data segment. The parameter mt indicates whether
 * to use the parallel(OpenMP) versions. Parallel versions are only used when
 * a single segment is used to hold the entire file - essentially a single-threaded
 * compression.
 * In other cases segments are handled in separate threads any way and we do not
 * need or want another level of parallelism to cause contention.
 */
int
compute_checksum(uchar_t *cksum_buf, int cksum, uchar_t *buf, uint64_t bytes, int mt, int verbose)
{
	DEBUG_STAT_EN(double strt, en);

#ifdef __HASH_COMPATIBILITY_
	assert(mt == 0 || mt == 1 || mt == 2);
#else
	assert(mt == 0 || mt == 1);
#endif

	DEBUG_STAT_EN(if (verbose) strt = get_wtime_millis());
	if (cksum == CKSUM_CRC64) {
		uint64_t *ck = (uint64_t *)cksum_buf;
		*ck = lzma_crc64(buf, bytes, 0);

	} else if (cksum == CKSUM_BLAKE256) {
		if (!mt) {
			if (bdsp.blake2b(cksum_buf, buf, NULL, 32, bytes, 0) != 0)
				return (-1);
		} else {
			if (bdsp.blake2bp(cksum_buf, buf, NULL, 32, bytes, 0) != 0)
				return (-1);
		}
	} else if (cksum == CKSUM_BLAKE512) {
		if (!mt) {
			if (bdsp.blake2b(cksum_buf, buf, NULL, 64, bytes, 0) != 0)
				return (-1);
		} else {
			if (bdsp.blake2bp(cksum_buf, buf, NULL, 64, bytes, 0) != 0)
				return (-1);
		}
	/*
	 * No parallelism for SKEIN. It is deprecated and retained here only for
	 * backwards compatiblity.
	 */
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
			if (!mt)
				ossl_SHA256(cksum_buf, buf, bytes);
			else if (mt == 1)
				ossl_SHA256_par(cksum_buf, buf, bytes);
#ifdef __HASH_COMPATIBILITY_
			else
				ossl_SHA256_par_old(cksum_buf, buf, bytes);
#endif
		} else {
			if (!mt)
				opt_SHA512t256(cksum_buf, buf, bytes);
			else if (mt == 1)
				opt_SHA512t256_par(cksum_buf, buf, bytes);
#ifdef __HASH_COMPATIBILITY_
			else
				opt_SHA512t256_par_old(cksum_buf, buf, bytes);
#endif
		}
	} else if (cksum == CKSUM_SHA512) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			if (!mt)
				ossl_SHA512(cksum_buf, buf, bytes);
			else if (mt == 1)
				ossl_SHA512_par(cksum_buf, buf, bytes);
#ifdef __HASH_COMPATIBILITY_
			else
				ossl_SHA512_par_old(cksum_buf, buf, bytes);
#endif
		} else {
			if (!mt)
				opt_SHA512(cksum_buf, buf, bytes);
			else if (mt == 1)
				opt_SHA512_par(cksum_buf, buf, bytes);
#ifdef __HASH_COMPATIBILITY_
			else
				opt_SHA512_par_old(cksum_buf, buf, bytes);
#endif
		}
	} else if (cksum == CKSUM_KECCAK256) {
		if (!mt) {
			if (Keccak256(cksum_buf, buf, bytes) != 0)
				return (-1);
		} else if (mt == 1) {
			if (Keccak256_par(cksum_buf, buf, bytes) != 0)
				return (-1);
#ifdef __HASH_COMPATIBILITY_
		} else {
			if (Keccak256_par_old(cksum_buf, buf, bytes) != 0)
				return (-1);
#endif
		}
	} else if (cksum == CKSUM_KECCAK512) {
		if (!mt) {
			if (Keccak512(cksum_buf, buf, bytes) != 0)
				return (-1);
		} else if (mt == 1) {
			if (Keccak512_par(cksum_buf, buf, bytes) != 0)
				return (-1);
#ifdef __HASH_COMPATIBILITY_
		} else {
			if (Keccak512_par_old(cksum_buf, buf, bytes) != 0)
				return (-1);
#endif
		}
	} else {
		return (-1);
	}
	DEBUG_STAT_EN(if (verbose) en = get_wtime_millis());
	DEBUG_STAT_EN(if (verbose) fprintf(stderr, "Checksum computed at %.3f MB/s\n", get_mb_s(bytes, strt, en)));
	return (0);
}

static void
init_sha512(void)
{
#ifdef	WORDS_BIGENDIAN
	cksum_provider = PROVIDER_OPENSSL;
#else
#ifdef	__x86_64__
	cksum_provider = PROVIDER_OPENSSL;
	if (proc_info.proc_type == PROC_X64_INTEL || proc_info.proc_type == PROC_X64_AMD) {
		if (opt_Init_SHA512(&proc_info) == 0) {
			cksum_provider = PROVIDER_X64_OPT;
		}
	}
#endif
#endif
}

static void
init_blake2(void)
{
	blake2_module_init(&bdsp, &proc_info);
}

void
list_checksums(FILE *strm, char *pad)
{
	int i;
	for (i=0; i<(sizeof (cksum_props)/sizeof (cksum_props[0])); i++) {
		if (!cksum_props[i].compatible)
			fprintf(strm, "%s%10s - %s\n", pad, cksum_props[i].name, cksum_props[i].desc);
	}
}

/*
 * Check if either the given checksum name or id is valid and
 * return it's properties.
 */
int
get_checksum_props(const char *name, int *cksum, int *cksum_bytes,
		  int *mac_bytes, int accept_comptible)
{
	int i;

	for (i=0; i<(sizeof (cksum_props)/sizeof (cksum_props[0])); i++) {
		if ((name != NULL && strcmp(name, cksum_props[i].name) == 0) ||
		    (*cksum != 0 && *cksum == cksum_props[i].cksum_id)) {
			if (!accept_comptible && cksum_props[i].compatible)
				break;
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
		++j;
	}
}

void
deserialize_checksum(uchar_t *checksum, uchar_t *buf, int cksum_bytes)
{
	int i,j;

	j = 0;
	for (i=cksum_bytes; i>0; i--) {
		checksum[i-1] = buf[j];
		++j;
	}
}

/*
 * Perform keyed hashing. With Skein/Blake/Keccak, HMAC is not used, rather
 * their native MAC features are used which are more optimal than HMAC.
 */
int
hmac_init(mac_ctx_t *mctx, int cksum, crypto_ctx_t *cctx)
{
	mctx->mac_cksum = cksum;

	if (cksum == CKSUM_BLAKE256) {
		blake2b_state *ctx = (blake2b_state *)malloc(sizeof (blake2b_state));
		if (!ctx) return (-1);
		if (bdsp.blake2b_init_key(ctx, 32, cctx->pkey, cctx->keylen) != 0)
			return (-1);
		mctx->mac_ctx = ctx;
		ctx = (blake2b_state *)malloc(sizeof (blake2b_state));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		memcpy(ctx, mctx->mac_ctx, sizeof (blake2b_state));
		mctx->mac_ctx_reinit = ctx;

	} else if (cksum == CKSUM_BLAKE512) {
		blake2b_state *ctx = (blake2b_state *)malloc(sizeof (blake2b_state));
		if (!ctx) return (-1);
		if (bdsp.blake2b_init_key(ctx, 64, cctx->pkey, cctx->keylen) != 0)
			return (-1);
		mctx->mac_ctx = ctx;
		ctx = (blake2b_state *)malloc(sizeof (blake2b_state));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		memcpy(ctx, mctx->mac_ctx, sizeof (blake2b_state));
		mctx->mac_ctx_reinit = ctx;

	} else if (cksum == CKSUM_SKEIN256) {
		Skein_512_Ctxt_t *ctx = (Skein_512_Ctxt_t *)malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) return (-1);
		Skein_512_InitExt(ctx, 256, SKEIN_CFG_TREE_INFO_SEQUENTIAL,
				 cctx->pkey, cctx->keylen);
		mctx->mac_ctx = ctx;
		ctx = (Skein_512_Ctxt_t *)malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		memcpy(ctx, mctx->mac_ctx, sizeof (Skein_512_Ctxt_t));
		mctx->mac_ctx_reinit = ctx;

	} else if (cksum == CKSUM_SKEIN512) {
		Skein_512_Ctxt_t *ctx = (Skein_512_Ctxt_t *)malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) return (-1);
		Skein_512_InitExt(ctx, 512, SKEIN_CFG_TREE_INFO_SEQUENTIAL,
				  cctx->pkey, cctx->keylen);
		mctx->mac_ctx = ctx;
		ctx = (Skein_512_Ctxt_t *)malloc(sizeof (Skein_512_Ctxt_t));
		if (!ctx) {
			free(mctx->mac_ctx);
			return (-1);
		}
		memcpy(ctx, mctx->mac_ctx, sizeof (Skein_512_Ctxt_t));
		mctx->mac_ctx_reinit = ctx;

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_CTX *ctx = (HMAC_CTX *)malloc(sizeof (HMAC_CTX));
			if (!ctx) return (-1);
			HMAC_CTX_init(ctx);
			HMAC_Init_ex(ctx, cctx->pkey, cctx->keylen, EVP_sha256(), NULL);
			mctx->mac_ctx = ctx;

			ctx = (HMAC_CTX *)malloc(sizeof (HMAC_CTX));
			if (!ctx) {
				free(mctx->mac_ctx);
				return (-1);
			}
			if (!HMAC_CTX_copy(ctx, (HMAC_CTX *)(mctx->mac_ctx))) {
				free(ctx);
				free(mctx->mac_ctx);
				return (-1);
			}
			mctx->mac_ctx_reinit = ctx;
		} else {
			HMAC_SHA512_Context *ctx = (HMAC_SHA512_Context *)malloc(sizeof (HMAC_SHA512_Context));
			if (!ctx) return (-1);
			opt_HMAC_SHA512t256_Init(ctx, cctx->pkey, cctx->keylen);
			mctx->mac_ctx = ctx;

			ctx = (HMAC_SHA512_Context *)malloc(sizeof (HMAC_SHA512_Context));
			if (!ctx) {
				free(mctx->mac_ctx);
				return (-1);
			}
			memcpy(ctx, mctx->mac_ctx, sizeof (HMAC_SHA512_Context));
			mctx->mac_ctx_reinit = ctx;
		}
	} else if (cksum == CKSUM_SHA512) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_CTX *ctx = (HMAC_CTX *)malloc(sizeof (HMAC_CTX));
			if (!ctx) return (-1);
			HMAC_CTX_init(ctx);
			HMAC_Init_ex(ctx, cctx->pkey, cctx->keylen, EVP_sha512(), NULL);
			mctx->mac_ctx = ctx;

			ctx = (HMAC_CTX *)malloc(sizeof (HMAC_CTX));
			if (!ctx) {
				free(mctx->mac_ctx);
				return (-1);
			}
			if (!HMAC_CTX_copy(ctx, (HMAC_CTX *)(mctx->mac_ctx))) {
				free(ctx);
				free(mctx->mac_ctx);
				return (-1);
			}
			mctx->mac_ctx_reinit = ctx;
		} else {
			HMAC_SHA512_Context *ctx = (HMAC_SHA512_Context *)malloc(sizeof (HMAC_SHA512_Context));
			if (!ctx) return (-1);
			opt_HMAC_SHA512_Init(ctx, cctx->pkey, cctx->keylen);
			mctx->mac_ctx = ctx;

			ctx = (HMAC_SHA512_Context *)malloc(sizeof (HMAC_SHA512_Context));
			if (!ctx) {
				free(mctx->mac_ctx);
				return (-1);
			}
			memcpy(ctx, mctx->mac_ctx, sizeof (HMAC_SHA512_Context));
			mctx->mac_ctx_reinit = ctx;
		}

	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		hashState *ctx = (hashState *)malloc(sizeof (hashState));
		if (!ctx) return (-1);

		if (cksum == CKSUM_KECCAK256) {
			if (Keccak_Init(ctx, 256) != 0)
				return (-1);
		} else {
			if (Keccak_Init(ctx, 512) != 0)
				return (-1);
		}
		if (Keccak_Update(ctx, cctx->pkey, cctx->keylen << 3) != 0)
			return (-1);
		mctx->mac_ctx = ctx;

		ctx = (hashState *)malloc(sizeof (hashState));
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

	if (cksum == CKSUM_BLAKE256 || cksum == CKSUM_BLAKE512) {
		memcpy(mctx->mac_ctx, mctx->mac_ctx_reinit, sizeof (blake2b_state));

	} else if (cksum == CKSUM_SKEIN256 || cksum == CKSUM_SKEIN512) {
		memcpy(mctx->mac_ctx, mctx->mac_ctx_reinit, sizeof (Skein_512_Ctxt_t));

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_SHA512 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_CTX_copy((HMAC_CTX *)(mctx->mac_ctx),
				      (HMAC_CTX *)(mctx->mac_ctx_reinit));
		} else {
			memcpy(mctx->mac_ctx, mctx->mac_ctx_reinit, sizeof (HMAC_SHA512_Context));
		}
	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		memcpy(mctx->mac_ctx, mctx->mac_ctx_reinit, sizeof (hashState));
	} else {
		return (-1);
	}
	return (0);
}

int
hmac_update(mac_ctx_t *mctx, uchar_t *data, uint64_t len)
{
	int cksum = mctx->mac_cksum;

	if (cksum == CKSUM_BLAKE256 || cksum == CKSUM_BLAKE512) {
		bdsp.blake2b_update((blake2b_state *)(mctx->mac_ctx), (uint8_t *)data, len);

	} else if (cksum == CKSUM_SKEIN256 || cksum == CKSUM_SKEIN512) {
		Skein_512_Update((Skein_512_Ctxt_t *)(mctx->mac_ctx), data, len);

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
#ifndef __OSSL_OLD__
			if (HMAC_Update((HMAC_CTX *)(mctx->mac_ctx), data, len) == 0)
				return (-1);
#else
			HMAC_Update((HMAC_CTX *)(mctx->mac_ctx), data, len);
#endif
		} else {
			opt_HMAC_SHA512t256_Update((HMAC_SHA512_Context *)(mctx->mac_ctx), data, len);
		}
	} else if (cksum == CKSUM_SHA512) {
		if (cksum_provider == PROVIDER_OPENSSL) {
#ifndef __OSSL_OLD__
			if (HMAC_Update((HMAC_CTX *)(mctx->mac_ctx), data, len) == 0)
				return (-1);
#else
			HMAC_Update((HMAC_CTX *)(mctx->mac_ctx), data, len);
#endif
		} else {
			opt_HMAC_SHA512_Update((HMAC_SHA512_Context *)(mctx->mac_ctx), data, len);
		}

	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		// Keccak takes data length in bits so we have to scale
		while (len > KECCAK_MAX_SEG) {
			uint64_t blen;

			blen = KECCAK_MAX_SEG;
			if (Keccak_Update((hashState *)(mctx->mac_ctx), data, blen << 3) != 0)
				return (-1);
			len -= KECCAK_MAX_SEG;
		}
		if (Keccak_Update((hashState *)(mctx->mac_ctx), data, len << 3) != 0)
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

	if (cksum == CKSUM_BLAKE256) {
		bdsp.blake2b_final((blake2b_state *)(mctx->mac_ctx), hash, 32);
		*len = 32;

	} else if (cksum == CKSUM_BLAKE512) {
		bdsp.blake2b_final((blake2b_state *)(mctx->mac_ctx), hash, 64);
		*len = 64;

	} else if (cksum == CKSUM_SKEIN256) {
		Skein_512_Final((Skein_512_Ctxt_t *)(mctx->mac_ctx), hash);
		*len = 32;

	} else if (cksum == CKSUM_SKEIN512) {
		Skein_512_Final((Skein_512_Ctxt_t *)(mctx->mac_ctx), hash);
		*len = 64;

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_Final((HMAC_CTX *)(mctx->mac_ctx), hash, len);
		} else {
			opt_HMAC_SHA512t256_Final((HMAC_SHA512_Context *)(mctx->mac_ctx), hash);
			*len = 32;
		}
	} else if (cksum == CKSUM_SHA512) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_Final((HMAC_CTX *)(mctx->mac_ctx), hash, len);
		} else {
			opt_HMAC_SHA512_Final((HMAC_SHA512_Context *)(mctx->mac_ctx), hash);
			*len = 64;
		}
	} else if (cksum == CKSUM_KECCAK256 || cksum == CKSUM_KECCAK512) {
		if (Keccak_Final((hashState *)(mctx->mac_ctx), hash) != 0)
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

	if (cksum == CKSUM_BLAKE256 || cksum == CKSUM_BLAKE512) {
		memset(mctx->mac_ctx, 0, sizeof (blake2b_state));
		memset(mctx->mac_ctx_reinit, 0, sizeof (blake2b_state));

	} else if (cksum == CKSUM_SKEIN256 || cksum == CKSUM_SKEIN512) {
		memset(mctx->mac_ctx, 0, sizeof (Skein_512_Ctxt_t));
		memset(mctx->mac_ctx_reinit, 0, sizeof (Skein_512_Ctxt_t));

	} else if (cksum == CKSUM_SHA256 || cksum == CKSUM_SHA512 || cksum == CKSUM_CRC64) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			HMAC_CTX_cleanup((HMAC_CTX *)(mctx->mac_ctx));
			HMAC_CTX_cleanup((HMAC_CTX *)(mctx->mac_ctx_reinit));
		} else {
			memset(mctx->mac_ctx, 0, sizeof (HMAC_SHA512_Context));
			memset(mctx->mac_ctx_reinit, 0, sizeof (HMAC_SHA512_Context));
		}
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

/*
 * Encryption related functions.
 */
int
init_crypto(crypto_ctx_t *cctx, uchar_t *pwd, int pwd_len, int crypto_alg,
	    uchar_t *salt, int saltlen, int keylen, uchar_t *nonce, int enc_dec)
{
	if (crypto_alg == CRYPTO_ALG_AES || crypto_alg == CRYPTO_ALG_SALSA20) {
		aes_ctx_t *actx;
		salsa20_ctx_t *sctx;

		/* Silence compiler warnings */
		actx = NULL;
		sctx = NULL;

		if (crypto_alg == CRYPTO_ALG_AES) {
			actx = (aes_ctx_t *)malloc(sizeof (aes_ctx_t));
			actx->keylen = keylen;
			cctx->pkey = actx->pkey;
			aes_module_init(&proc_info);
		} else {
			sctx = (salsa20_ctx_t *)malloc(sizeof (salsa20_ctx_t));
			sctx->keylen = keylen;
			cctx->pkey = sctx->pkey;
		}
		cctx->keylen = keylen;

		if (enc_dec) {
			/*
			 * Encryption init.
			 */
			cctx->salt = (uchar_t *)malloc(32);
			salt = cctx->salt;
			cctx->saltlen = 32;
			if (RAND_status() != 1 || RAND_bytes(salt, 32) != 1) {
				if (geturandom_bytes(salt, 32) != 0) {
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
					compute_checksum(&sb[b], CKSUM_SHA256, sb, b, 0, 0);
					b = 8 + 4;
					*((uint32_t *)&sb[b]) = rand();
					compute_checksum(salt, CKSUM_SHA256, &sb[b], 32 + 4, 0, 0);
				}
			}

			/*
			 * Zero nonce (arg #6) since it will be generated.
			 */
			if (crypto_alg == CRYPTO_ALG_AES) {
				if (aes_init(actx, salt, 32, pwd, pwd_len, 0, enc_dec) != 0) {
					fprintf(stderr, "Failed to initialize AES context\n");
					return (-1);
				}
			} else {
				if (salsa20_init(sctx, salt, 32, pwd, pwd_len, 0, enc_dec) != 0) {
					fprintf(stderr, "Failed to initialize SALSA20 context\n");
					return (-1);
				}
			}
		} else {
			/*
			 * Decryption init.
			 * Pass given nonce and salt.
			 */
			if (saltlen > MAX_SALTLEN) {
				fprintf(stderr, "Salt too long. Max allowed length is %d\n",
				    MAX_SALTLEN);
				free(actx);
				return (-1);
			}
			cctx->salt = (uchar_t *)malloc(saltlen);
			memcpy(cctx->salt, salt, saltlen);

			if (crypto_alg == CRYPTO_ALG_AES) {
				if (aes_init(actx, cctx->salt, saltlen, pwd, pwd_len, *((uint64_t *)nonce),
				    enc_dec) != 0) {
					fprintf(stderr, "Failed to initialize AES context\n");
					return (-1);
				}
			} else {
				if (salsa20_init(sctx, salt, 32, pwd, pwd_len, nonce, enc_dec) != 0) {
					fprintf(stderr, "Failed to initialize SALSA20 context\n");
					return (-1);
				}
			}
		}
		if (crypto_alg == CRYPTO_ALG_AES) {
			cctx->crypto_ctx = actx;
		} else {
			cctx->crypto_ctx = sctx;
		}
		cctx->crypto_alg = crypto_alg;
		cctx->enc_dec = enc_dec;
		actx = NULL;
		sctx = NULL;
	} else {
		fprintf(stderr, "Unrecognized algorithm code: %d\n", crypto_alg);
		return (-1);
	}
	return (0);
}

int
crypto_buf(crypto_ctx_t *cctx, uchar_t *from, uchar_t *to, uint64_t bytes, uint64_t id)
{
	if (cctx->crypto_alg == CRYPTO_ALG_AES) {
		if (cctx->enc_dec == ENCRYPT_FLAG) {
			return (aes_encrypt((aes_ctx_t *)(cctx->crypto_ctx), from, to, bytes, id));
		} else {
			return (aes_decrypt((aes_ctx_t *)(cctx->crypto_ctx), from, to, bytes, id));
		}
	} else if (cctx->crypto_alg == CRYPTO_ALG_SALSA20) {
		if (cctx->enc_dec == ENCRYPT_FLAG) {
			return (salsa20_encrypt((salsa20_ctx_t *)(cctx->crypto_ctx), from, to, bytes, id));
		} else {
			return (salsa20_decrypt((salsa20_ctx_t *)(cctx->crypto_ctx), from, to, bytes, id));
		}
	} else {
		fprintf(stderr, "Unrecognized algorithm code: %d\n", cctx->crypto_alg);
		return (-1);
	}
	return (0);
}

uchar_t *
crypto_nonce(crypto_ctx_t *cctx)
{
	if (cctx->crypto_alg == CRYPTO_ALG_AES) {
		return (aes_nonce((aes_ctx_t *)(cctx->crypto_ctx)));
	}
	return (salsa20_nonce((salsa20_ctx_t *)(cctx->crypto_ctx)));
}

void
crypto_clean_pkey(crypto_ctx_t *cctx)
{
	if (cctx->crypto_alg == CRYPTO_ALG_AES) {
		aes_clean_pkey((aes_ctx_t *)(cctx->crypto_ctx));
	} else {
		salsa20_clean_pkey((salsa20_ctx_t *)(cctx->crypto_ctx));
	}
	cctx->pkey = NULL;
}

void
cleanup_crypto(crypto_ctx_t *cctx)
{
	if (cctx->crypto_alg == CRYPTO_ALG_AES) {
		aes_cleanup((aes_ctx_t *)(cctx->crypto_ctx));
	} else {
		salsa20_cleanup((salsa20_ctx_t *)(cctx->crypto_ctx));
	}
	memset(cctx->salt, 0, 32);
	free(cctx->salt);
	free(cctx);
}

int
geturandom_bytes(uchar_t *rbytes, int buflen)
{
	int fd;
	int64_t lenread;
	uchar_t * buf = rbytes;

	/* Open /dev/urandom. Upto 10 retries. */
	fd = -1;
	lenread = 1;
	while (fd == -1 && lenread < 10) {
		if ((fd = open("/dev/urandom", O_RDONLY)) != -1)
			break;
		lenread++;
		sleep(1);
	}
	if (fd == -1)
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

/*
 * Input password string from terminal without echoing.
 */
int
get_pw_string(uchar_t pw[MAX_PW_LEN], const char *prompt, int twice)
{
	int fd, len;
	FILE *input, *strm;
	struct termios oldt, newt;
	char pw1[MAX_PW_LEN], pw2[MAX_PW_LEN], *s;

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

	if (s == NULL || strlen(s) < 2) {
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
	strcpy((char *)pw, (const char *)pw1);
	memset(pw1, 0, MAX_PW_LEN);
	memset(pw2, 0, MAX_PW_LEN);
	return (len);
}
