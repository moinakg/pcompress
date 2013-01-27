/*-
 * Copyright (c) 2001-2003 Allan Saddi <allan@saddi.com>
 * Copyright (c) 2012 Moinak Ghosh moinakg <at1> gm0il <dot> com
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
 */

#ifndef _APS_SHA512_H
#define _APS_SHA512_H

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <utils.h>

#define	SHA512_HASH_SIZE		64
#define	SHA512t256_HASH_SIZE	32
#define	SHA512_BLOCK_SIZE	128L

/* Hash size in 64-bit words */
#define SHA512_HASH_WORDS 8
#define SHA512t256_HASH_WORDS 4

typedef struct _SHA512_Context {
  uint64_t totalLength[2], blocks;
  uint64_t hash[SHA512_HASH_WORDS];
  uint32_t bufferLength;
  union {
    uint64_t words[SHA512_BLOCK_SIZE/8];
    uint8_t bytes[SHA512_BLOCK_SIZE];
  } buffer;
} SHA512_Context;

typedef struct {
  SHA512_Context outer;
  SHA512_Context inner;
} HMAC_SHA512_Context;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APS_NAMESPACE
#define APS_NAMESPACE(name) opt_##name
#endif /* !APS_NAMESPACE */

void APS_NAMESPACE(SHA512_Init) (SHA512_Context *sc);
void APS_NAMESPACE(SHA512_Update) (SHA512_Context *sc, const void *data, size_t len);
void APS_NAMESPACE(SHA512_Final) (SHA512_Context *sc, uint8_t hash[SHA512_HASH_SIZE]);
int  APS_NAMESPACE(Init_SHA512) (processor_info_t *pc);

/* As are SHA-512/256 and SHA-512/224 */
#define SHA512t256_Context SHA512_Context
void APS_NAMESPACE(SHA512t256_Init) (SHA512_Context *sc);
void APS_NAMESPACE(SHA512t256_Update) (SHA512_Context *sc, const void *data, size_t len);
void APS_NAMESPACE(SHA512t256_Final) (SHA512_Context *sc, uint8_t hash[SHA512t256_HASH_SIZE]);

void APS_NAMESPACE(HMAC_SHA512_Init) (HMAC_SHA512_Context *ctxt, const void *key, size_t keyLen);
void APS_NAMESPACE(HMAC_SHA512_Update) (HMAC_SHA512_Context *ctxt, const void *data, size_t len);
void APS_NAMESPACE(HMAC_SHA512_Final) (HMAC_SHA512_Context *ctxt, uint8_t hmac[SHA512_HASH_SIZE]);

void APS_NAMESPACE(HMAC_SHA512t256_Init) (HMAC_SHA512_Context *ctxt, const void *key, size_t keyLen);
void APS_NAMESPACE(HMAC_SHA512t256_Update) (HMAC_SHA512_Context *ctxt, const void *data, size_t len);
void APS_NAMESPACE(HMAC_SHA512t256_Final) (HMAC_SHA512_Context *ctxt, uint8_t hmac[SHA512t256_HASH_SIZE]);

/*
 * Intel's optimized SHA512 core routines. These routines are described in an
 * Intel White-Paper:
 * "Fast SHA-512 Implementations on Intel Architecture Processors"
 * Note: Works on AMD Bulldozer and later as well.
 */
extern void sha512_sse4(const void *input_data, void *digest, uint64_t num_blks);
extern void sha512_avx(const void *input_data, void *digest, uint64_t num_blks);

#ifdef __cplusplus
}
#endif

#endif /* !_APS_SHA512_H */
