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

/*
 * Define WORDS_BIGENDIAN if compiling on a big-endian architecture.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <pthread.h>
#include <string.h>
#include <utils.h>
#include <sha256.h>

#ifdef WORDS_BIGENDIAN

#define BYTESWAP(x) (x)
#define BYTESWAP64(x) (x)

#else /* WORDS_BIGENDIAN */

#define BYTESWAP(x) htonl(x)
#define BYTESWAP64(x) htonll(x)

#endif /* WORDS_BIGENDIAN */
typedef void (*update_func_ptr)(void *input_data, uint32_t digest[8], uint64_t num_blks);

static uint8_t padding[64] = {
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint32_t iv256[SHA256_HASH_WORDS] = {
  0x6a09e667L,
  0xbb67ae85L,
  0x3c6ef372L,
  0xa54ff53aL,
  0x510e527fL,
  0x9b05688cL,
  0x1f83d9abL,
  0x5be0cd19L
};

static update_func_ptr sha_update_func;

int
APS_NAMESPACE(Init_SHA) (processor_info_t *pc)
{
	if (pc->proc_type == PROC_X64_INTEL) {
		if (pc->avx_level > 0) {
			sha_update_func = sha256_avx;
			
		} else if (pc->sse_level >= 4) {
			sha_update_func = sha256_sse4;
			
		} else {
			return (1);
		}
		return (0);
	}
	return (1);
}

static void
_init (SHA256_Context *sc, const uint32_t iv[SHA256_HASH_WORDS])
{
	int i;

	/*
	 * SHA256_HASH_WORDS is 8, must be 8, cannot be anything but 8!
	 * So we unroll a loop here.
	 */
	sc->hash[0] = iv[0];
	sc->hash[1] = iv[1];
	sc->hash[2] = iv[2];
	sc->hash[3] = iv[3];
	sc->hash[4] = iv[4];
	sc->hash[5] = iv[5];
	sc->hash[6] = iv[6];
	sc->hash[7] = iv[7];

	sc->totalLength = 0LL;
	sc->bufferLength = 0L;
}

void
APS_NAMESPACE(SHA256_Init) (SHA256_Context *sc)
{
	_init (sc, iv256);
}

void
APS_NAMESPACE(SHA256_Update) (SHA256_Context *sc, const void *vdata, size_t len)
{
	const uint8_t *data = vdata;
	uint32_t bufferBytesLeft;
	size_t bytesToCopy;
	int rem;

	if (sc->bufferLength) {
		do {
			bufferBytesLeft = 64L - sc->bufferLength;
			bytesToCopy = bufferBytesLeft;
			if (bytesToCopy > len)
				bytesToCopy = len;

			memcpy (&sc->buffer.bytes[sc->bufferLength], data, bytesToCopy);
			sc->totalLength += bytesToCopy * 8L;
			sc->bufferLength += bytesToCopy;
			data += bytesToCopy;
			len -= bytesToCopy;

			if (sc->bufferLength == 64L) {
				sc->blocks = 1;
				sha_update_func(sc->buffer.words, sc->hash, sc->blocks);
				sc->bufferLength = 0L;
			} else {
				return;
			}
		} while (len > 0 && len <= 64L);
		if (!len) return;
	}

	sc->blocks = len >> 6;
	rem = len - (sc->blocks << 6);
	len = sc->blocks << 6;
	sc->totalLength += rem * 8L;

	if (len) {
		sc->totalLength += len * 8L;
		sha_update_func((uint32_t *)data, sc->hash, sc->blocks);
	}
	if (rem) {
		memcpy (&sc->buffer.bytes[0], data + len, rem);
		sc->bufferLength = rem;
	}
}

static void
_final (SHA256_Context *sc, uint8_t *hash, int hashWords)
{
	uint32_t bytesToPad;
	uint64_t lengthPad;
	int i;

	bytesToPad = 120L - sc->bufferLength;
	if (bytesToPad > 64L)
		bytesToPad -= 64L;

	lengthPad = BYTESWAP64(sc->totalLength);

	APS_NAMESPACE(SHA256_Update) (sc, padding, bytesToPad);
	APS_NAMESPACE(SHA256_Update) (sc, &lengthPad, 8L);

	if (hash) {
		for (i = 0; i < hashWords; i++) {
			hash[0] = (uint8_t) (sc->hash[i] >> 24);
			hash[1] = (uint8_t) (sc->hash[i] >> 16);
			hash[2] = (uint8_t) (sc->hash[i] >> 8);
			hash[3] = (uint8_t) sc->hash[i];
			hash += 4;
		}
	}
}

void
APS_NAMESPACE(SHA256_Final) (SHA256_Context *sc, uint8_t hash[SHA256_HASH_SIZE])
{
	_final (sc, hash, SHA256_HASH_WORDS);
}

/* Initialize an HMAC-SHA256 operation with the given key. */
void
APS_NAMESPACE(HMAC_SHA256_Init) (HMAC_SHA256_Context * ctx, const void * _K, size_t Klen)
{
	unsigned char pad[64];
	unsigned char khash[32];
	const unsigned char * K = _K;
	size_t i;

	/* If Klen > 64, the key is really SHA256(K). */
	if (Klen > 64) {
		APS_NAMESPACE(SHA256_Init)(&ctx->ictx);
		APS_NAMESPACE(SHA256_Update)(&ctx->ictx, K, Klen);
		APS_NAMESPACE(SHA256_Final)(&ctx->ictx, khash);
		K = khash;
		Klen = 32;
	}

	/* Inner SHA256 operation is SHA256(K xor [block of 0x36] || data). */
	APS_NAMESPACE(SHA256_Init)(&ctx->ictx);
	memset(pad, 0x36, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	APS_NAMESPACE(SHA256_Update)(&ctx->ictx, pad, 64);

	/* Outer SHA256 operation is SHA256(K xor [block of 0x5c] || hash). */
	APS_NAMESPACE(SHA256_Init)(&ctx->octx);
	memset(pad, 0x5c, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	APS_NAMESPACE(SHA256_Update)(&ctx->octx, pad, 64);

	/* Clean the stack. */
	memset(khash, 0, 32);
}

/* Add bytes to the HMAC-SHA256 operation. */
void
APS_NAMESPACE(HMAC_SHA256_Update) (HMAC_SHA256_Context * ctx, const void *in, size_t len)
{
	/* Feed data to the inner SHA256 operation. */
	APS_NAMESPACE(SHA256_Update)(&ctx->ictx, in, len);
}

/* Finish an HMAC-SHA256 operation. */
void
APS_NAMESPACE(HMAC_SHA256_Final) (HMAC_SHA256_Context * ctx, unsigned char digest[32])
{
	unsigned char ihash[32];

	/* Finish the inner SHA256 operation. */
	APS_NAMESPACE(SHA256_Final)(&ctx->ictx, ihash);

	/* Feed the inner hash to the outer SHA256 operation. */
	APS_NAMESPACE(SHA256_Update)(&ctx->octx, ihash, 32);

	/* Finish the outer SHA256 operation. */
	APS_NAMESPACE(SHA256_Final)(&ctx->octx, digest);

	/* Clean the stack. */
	memset(ihash, 0, 32);
}
