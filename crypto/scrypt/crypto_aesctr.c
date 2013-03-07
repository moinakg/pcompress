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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/aes.h>
#ifdef __USE_SSE_INTRIN__
#include <emmintrin.h>
#endif
#include <utils.h>

#include "crypto_aesctr.h"

extern setkey_func_ptr enc_setkey;
extern encrypt_func_ptr enc_encrypt;

struct crypto_aesctr {
	AES_KEY * key;
	uint64_t nonce;
	uint64_t bytectr;
	uint8_t buf[16] __attribute__((aligned(16)));
};

/**
 * crypto_aesctr_init(key, nonce):
 * Prepare to encrypt/decrypt data with AES in CTR mode, using the provided
 * expanded key and nonce.  The key provided must remain valid for the
 * lifetime of the stream.
 */
struct crypto_aesctr *
crypto_aesctr_init(AES_KEY * key, uint64_t nonce)
{
	struct crypto_aesctr * stream;

	/* Allocate memory. */
	if ((stream = (struct crypto_aesctr *)malloc(sizeof(struct crypto_aesctr))) == NULL)
		goto err0;

	/* Initialize values. */
	stream->key = key;
	stream->nonce = nonce;
	stream->bytectr = 0;

	/* Success! */
	return (stream);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * crypto_aesctr_stream(stream, inbuf, outbuf, buflen):
 * Generate the next ${buflen} bytes of the AES-CTR stream and xor them with
 * bytes from ${inbuf}, writing the result into ${outbuf}.  If the buffers
 * ${inbuf} and ${outbuf} overlap, they must be identical.
 */
void
crypto_aesctr_stream(struct crypto_aesctr * stream, const uint8_t * inbuf,
    uint8_t * outbuf, size_t buflen)
{
	uint8_t pblk[16];
	size_t pos;
	int bytemod, last;

	last = 0;
	pos = 0;
	*((uint64_t *)pblk) = htonll(stream->nonce);

do_last:
	for (; pos < buflen; pos++) {
		/* How far through the buffer are we? */
		bytemod = stream->bytectr & (16 - 1);

		/* Generate a block of cipherstream if needed. */
		if (bytemod == 0) {
			*((uint64_t *)(pblk + 8)) = htonll(stream->bytectr / 16);
			enc_encrypt(pblk, stream->buf, stream->key);
#ifdef __USE_SSE_INTRIN__
			if (!last)
				break;
#endif
		}

		/* Encrypt a byte. */
		outbuf[pos] = inbuf[pos] ^ stream->buf[bytemod];

		/* Move to the next byte of cipherstream. */
		stream->bytectr += 1;
	}
#ifdef __USE_SSE_INTRIN__
	if (last) {
		memset(pblk, 0, 16);
		return;
	}
	for (; pos < buflen-15; pos += 16) {
		__m128i cblk, dat, odat;

		PREFETCH_WRITE(outbuf+pos, 0);
		PREFETCH_READ(inbuf+pos, 0);
		cblk = _mm_load_si128((__m128i *)(stream->buf));
		dat = _mm_loadu_si128((__m128i *)(inbuf+pos));
		odat = _mm_xor_si128(cblk, dat);
		_mm_storeu_si128((__m128i *)(outbuf+pos), odat);
		stream->bytectr += 16;
		*((uint64_t *)(pblk + 8)) = htonll(stream->bytectr / 16);
		enc_encrypt(pblk, stream->buf, stream->key);
	}
	last = 1;
	goto do_last;
#endif
}

/**
 * crypto_aesctr_free(stream):
 * Free the provided stream object.
 */
void
crypto_aesctr_free(struct crypto_aesctr * stream)
{
	int i;

	/* Zero potentially sensitive information. */
	for (i = 0; i < 16; i++)
		stream->buf[i] = 0;
	stream->bytectr = stream->nonce = 0;

	/* Free the stream. */
	free(stream);
}
