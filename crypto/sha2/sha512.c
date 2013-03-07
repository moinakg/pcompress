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
#include "sha512.h"


#ifdef WORDS_BIGENDIAN

#define BYTESWAP(x) (x)
#define BYTESWAP64(x) (x)

#else /* WORDS_BIGENDIAN */

#define BYTESWAP(x) htonl(x)
#define BYTESWAP64(x) htonll(x)

#endif /* WORDS_BIGENDIAN */

typedef void (*update_func_ptr)(const void *input_data, void *digest, uint64_t num_blks);

static const uint8_t padding[SHA512_BLOCK_SIZE] = {
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint64_t iv512[SHA512_HASH_WORDS] = {
  0x6a09e667f3bcc908LL,
  0xbb67ae8584caa73bLL,
  0x3c6ef372fe94f82bLL,
  0xa54ff53a5f1d36f1LL,
  0x510e527fade682d1LL,
  0x9b05688c2b3e6c1fLL,
  0x1f83d9abfb41bd6bLL,
  0x5be0cd19137e2179LL
};

static const uint64_t iv256[SHA512_HASH_WORDS] = {
  0x22312194fc2bf72cLL,
  0x9f555fa3c84c64c2LL,
  0x2393b86b6f53b151LL,
  0x963877195940eabdLL,
  0x96283ee2a88effe3LL,
  0xbe5e1e2553863992LL,
  0x2b0199fc2c85b8aaLL,
  0x0eb72ddc81c52ca2LL
};

static update_func_ptr sha512_update_func;

int
APS_NAMESPACE(Init_SHA512) (processor_info_t *pc)
{
	if (pc->proc_type == PROC_X64_INTEL || pc->proc_type == PROC_X64_AMD) {
		if (pc->avx_level > 0) {
			sha512_update_func = sha512_avx;

		} else if (pc->sse_level >= 4) {
			sha512_update_func = sha512_sse4;

		} else {
			return (1);
		}
		return (0);
	}
	return (1);
}

static void
_init (SHA512_Context *sc, const uint64_t iv[SHA512_HASH_WORDS])
{
	int i;

	sc->totalLength[0] = 0LL;
	sc->totalLength[1] = 0LL;
	for (i = 0; i < SHA512_HASH_WORDS; i++)
		sc->hash[i] = iv[i];
	sc->bufferLength = 0L;
}

void
APS_NAMESPACE(SHA512_Init) (SHA512_Context *sc)
{
	_init (sc, iv512);
}

void
APS_NAMESPACE(SHA512t256_Init) (SHA512_Context *sc)
{
	_init (sc, iv256);
}

void
APS_NAMESPACE(SHA512_Update) (SHA512_Context *sc, const void *vdata, size_t len)
{
	const uint8_t *data = (const uint8_t *)vdata;
	uint32_t bufferBytesLeft;
	size_t bytesToCopy;
	int rem;
	uint64_t carryCheck;

	if (sc->bufferLength) {
		do {
			bufferBytesLeft = SHA512_BLOCK_SIZE - sc->bufferLength;
			bytesToCopy = bufferBytesLeft;
			if (bytesToCopy > len)
				bytesToCopy = len;

			memcpy (&sc->buffer.bytes[sc->bufferLength], data, bytesToCopy);
			carryCheck = sc->totalLength[1];
			sc->totalLength[1] += bytesToCopy * 8L;
			if (sc->totalLength[1] < carryCheck)
				sc->totalLength[0]++;

			sc->bufferLength += bytesToCopy;
			data += bytesToCopy;
			len -= bytesToCopy;

			if (sc->bufferLength == SHA512_BLOCK_SIZE) {
				sc->blocks = 1;
				sha512_update_func(sc->buffer.words, sc->hash, sc->blocks);
				sc->bufferLength = 0L;
			} else {
				return;
			}
		} while (len > 0 && len <= SHA512_BLOCK_SIZE);
		if (!len) return;
	}

	sc->blocks = len >> 7;
	rem = len - (sc->blocks << 7);
	len = sc->blocks << 7;
	carryCheck = sc->totalLength[1];
	sc->totalLength[1] += rem * 8L;
	if (sc->totalLength[1] < carryCheck)
		sc->totalLength[0]++;

	if (len) {
		carryCheck = sc->totalLength[1];
		sc->totalLength[1] += len * 8L;
		if (sc->totalLength[1] < carryCheck)
			sc->totalLength[0]++;
		sha512_update_func((uint32_t *)data, sc->hash, sc->blocks);
	}
	if (rem) {
		memcpy (&sc->buffer.bytes[0], data + len, rem);
		sc->bufferLength = rem;
	}
}

void
APS_NAMESPACE(SHA512t256_Update) (SHA512_Context *sc, const void *data, size_t len)
{
	APS_NAMESPACE(SHA512_Update) (sc, data, len);
}

static void
_final (SHA512_Context *sc, uint8_t *hash, int hashWords, int halfWord)
{
	uint32_t bytesToPad;
	uint64_t lengthPad[2];
	int i;
	
	bytesToPad = 240L - sc->bufferLength;
	if (bytesToPad > SHA512_BLOCK_SIZE)
		bytesToPad -= SHA512_BLOCK_SIZE;
	
	lengthPad[0] = BYTESWAP64(sc->totalLength[0]);
	lengthPad[1] = BYTESWAP64(sc->totalLength[1]);
	
	APS_NAMESPACE(SHA512_Update) (sc, padding, bytesToPad);
	APS_NAMESPACE(SHA512_Update) (sc, lengthPad, 16L);
	
	if (hash) {
		for (i = 0; i < hashWords; i++) {
			*((uint64_t *) hash) = BYTESWAP64(sc->hash[i]);
			hash += 8;
		}
		if (halfWord) {
			hash[0] = (uint8_t) (sc->hash[i] >> 56);
			hash[1] = (uint8_t) (sc->hash[i] >> 48);
			hash[2] = (uint8_t) (sc->hash[i] >> 40);
			hash[3] = (uint8_t) (sc->hash[i] >> 32);
		}
	}
}

void
APS_NAMESPACE(SHA512_Final) (SHA512_Context *sc, uint8_t hash[SHA512_HASH_SIZE])
{
	_final (sc, hash, SHA512_HASH_WORDS, 0);
}

void
APS_NAMESPACE(SHA512t256_Final) (SHA512_Context *sc, uint8_t hash[SHA512t256_HASH_SIZE])
{
	_final (sc, hash, SHA512t256_HASH_WORDS, 0);
}

#define HASH_CONTEXT SHA512_Context
#define HASH_INIT APS_NAMESPACE(SHA512_Init)
#define HASH_UPDATE APS_NAMESPACE(SHA512_Update)
#define HASH_FINAL APS_NAMESPACE(SHA512_Final)
#define HASH_SIZE SHA512_HASH_SIZE
#define HASH_BLOCK_SIZE SHA512_BLOCK_SIZE

#define HMAC_CONTEXT HMAC_SHA512_Context
#define HMAC_INIT APS_NAMESPACE(HMAC_SHA512_Init)
#define HMAC_UPDATE APS_NAMESPACE(HMAC_SHA512_Update)
#define HMAC_FINAL APS_NAMESPACE(HMAC_SHA512_Final)

#include "_hmac.c"

#undef HASH_CONTEXT
#undef HASH_INIT
#undef HASH_UPDATE
#undef HASH_FINAL
#undef HASH_SIZE
#undef HASH_BLOCK_SIZE
#undef HMAC_CONTEXT
#undef HMAC_INIT
#undef HMAC_UPDATE
#undef HMAC_FINAL

#define HASH_CONTEXT SHA512_Context
#define HASH_INIT APS_NAMESPACE(SHA512t256_Init)
#define HASH_UPDATE APS_NAMESPACE(SHA512t256_Update)
#define HASH_FINAL APS_NAMESPACE(SHA512t256_Final)
#define HASH_SIZE SHA512t256_HASH_SIZE
#define HASH_BLOCK_SIZE SHA512_BLOCK_SIZE

#define HMAC_CONTEXT HMAC_SHA512_Context
#define HMAC_INIT APS_NAMESPACE(HMAC_SHA512t256_Init)
#define HMAC_UPDATE APS_NAMESPACE(HMAC_SHA512t256_Update)
#define HMAC_FINAL APS_NAMESPACE(HMAC_SHA512t256_Final)

#include "_hmac.c"

