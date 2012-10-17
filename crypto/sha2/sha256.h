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

#ifndef _APS_SHA256_H
#define _APS_SHA256_H

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <utils.h>

#define SHA256_HASH_SIZE 32

/* Hash size in 32-bit words */
#define SHA256_HASH_WORDS 8

typedef struct _SHA256_Context {
	uint64_t totalLength, blocks;
	uint32_t hash[SHA256_HASH_WORDS];
	uint32_t bufferLength;
	union {
		uint32_t words[16];
		uint8_t bytes[64];
	} buffer;
} SHA256_Context;

typedef struct HMAC_SHA256Context {
	SHA256_Context ictx;
	SHA256_Context octx;
} HMAC_SHA256_Context;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APS_NAMESPACE
#define APS_NAMESPACE(name) opt_##name
#endif /* !APS_NAMESPACE */

void APS_NAMESPACE(SHA256_Init) (SHA256_Context *sc);
void APS_NAMESPACE(SHA256_Update) (SHA256_Context *sc, const void *data, size_t len);
void APS_NAMESPACE(SHA256_Final) (SHA256_Context *sc, uint8_t hash[SHA256_HASH_SIZE]);
int  APS_NAMESPACE(Init_SHA) (processor_info_t *pc);

void APS_NAMESPACE(HMAC_SHA256_Init) (HMAC_SHA256_Context * ctx, const void * _K, size_t Klen);
void APS_NAMESPACE(HMAC_SHA256_Update) (HMAC_SHA256_Context * ctx, const void *in, size_t len);
void APS_NAMESPACE(HMAC_SHA256_Final) (HMAC_SHA256_Context * ctx, unsigned char digest[32]);

/*
 * Intel's optimized SHA256 core routines. These routines are described in an
 * Intel White-Paper:
 * "Fast SHA-256 Implementations on Intel Architecture Processors"
 */
extern void sha256_sse4(void *input_data, uint32_t digest[8], uint64_t num_blks);
extern void sha256_avx(void *input_data, uint32_t digest[8], uint64_t num_blks);

#ifdef __cplusplus
}
#endif

#endif /* !_APS_SHA256_H */
