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

#include <sys/types.h>
#include <stdlib.h>
#include <openssl/sha.h>
#include <sha512.h>
#include <stdio.h>
#include <string.h>

#if defined(_OPENMP)
#include <omp.h>
#endif
#include <utils.h>

#define	BLKSZ		(2048)

/*
 * Helper functions for single-call SHA2 hashing. Both serial and
 * parallel versions are provided. Parallel versions use 2-stage
 * Merkle Tree hashing.
 * 
 * At the leaf level data is split into BLKSZ blocks and 4 threads
 * compute 4 hashes of interleaved block streams. At 2nd level two
 * new hashes are generated from hashing the 2 pairs of hash values.
 * In the final stage the 2 hash values are hashed to the final digest.
 * 
 * References:
 * http://eprint.iacr.org/2012/476.pdf
 * http://gva.noekeon.org/papers/bdpv09tree.html
 */
void
ossl_SHA256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	SHA256_CTX ctx;

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, buf, bytes);
	SHA256_Final(cksum_buf, &ctx);
}

void
ossl_SHA256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t cksum[6][32];
	SHA256_CTX ctx[4];
	int i, rem;
	uint64_t _bytes;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes <= BLKSZ * 2) {
		SHA256_Init(&ctx[0]);
		SHA256_Update(&ctx[0], buf, bytes);
		SHA256_Final(cksum_buf, &ctx[0]);
		return;
	}

	/*
	 * Do first level hashes in parallel.
	 */
	_bytes = (bytes / BLKSZ) * BLKSZ;
	rem = bytes - _bytes;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 4; ++i)
	{
		uint64_t byt;

		byt = i * BLKSZ;
		SHA256_Init(&ctx[i]);
		while (byt < _bytes) {
			SHA256_Update(&ctx[i], buf + byt, BLKSZ);
			byt += 4 * BLKSZ;
		}
		if (i>0)
			SHA256_Final(cksum[i], &ctx[i]);
	}
	if (rem > 0) {
		SHA256_Update(&ctx[0], buf + bytes - rem, rem);
	}
	SHA256_Final(cksum[0], &ctx[0]);

	/*
	 * Second level hashes.
	 */
	SHA256_Init(&ctx[0]);
	SHA256_Init(&ctx[1]);
	SHA256_Update(&ctx[0], &cksum[0], 2 * 32);
	SHA256_Update(&ctx[1], &cksum[1], 2 * 32);
	SHA256_Final(cksum[4], &ctx[0]);
	SHA256_Final(cksum[5], &ctx[1]);

	/*
	 * Final hash.
	 */
	SHA256_Init(&ctx[0]);
	SHA256_Update(&ctx[0], &cksum[4], 2 * 32);
	SHA256_Final(cksum_buf, &ctx[0]);
}

void
ossl_SHA512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	SHA512_CTX ctx;

	SHA512_Init(&ctx);
	SHA512_Update(&ctx, buf, bytes);
	SHA512_Final(cksum_buf, &ctx);
}

void
ossl_SHA512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t cksum[6][32];
	SHA512_CTX ctx[4];
	int i, rem;
	uint64_t _bytes;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes <= BLKSZ * 2) {
		SHA512_Init(&ctx[0]);
		SHA512_Update(&ctx[0], buf, bytes);
		SHA512_Final(cksum_buf, &ctx[0]);
		return;
	}

	/*
	 * Do first level hashes in parallel.
	 */
	_bytes = (bytes / BLKSZ) * BLKSZ;
	rem = bytes - _bytes;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 4; ++i)
	{
		uint64_t byt;

		byt = i * BLKSZ;
		SHA512_Init(&ctx[i]);
		while (byt < _bytes) {
			SHA512_Update(&ctx[i], buf + byt, BLKSZ);
			byt += 4 * BLKSZ;
		}
		if (i>0)
			SHA512_Final(cksum[i], &ctx[i]);
	}
	if (rem > 0) {
		SHA512_Update(&ctx[0], buf + bytes - rem, rem);
	}
	SHA512_Final(cksum[0], &ctx[0]);

	/*
	 * Second level hashes.
	 */
	SHA512_Init(&ctx[0]);
	SHA512_Init(&ctx[1]);
	SHA512_Update(&ctx[0], &cksum[0], 2 * 32);
	SHA512_Update(&ctx[1], &cksum[1], 2 * 32);
	SHA512_Final(cksum[4], &ctx[0]);
	SHA512_Final(cksum[5], &ctx[1]);

	/*
	 * Final hash.
	 */
	SHA512_Init(&ctx[0]);
	SHA512_Update(&ctx[0], &cksum[4], 2 * 32);
	SHA512_Final(cksum_buf, &ctx[0]);
}

void
opt_SHA512t256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	SHA512_Context ctx;

	opt_SHA512t256_Init(&ctx);
	opt_SHA512t256_Update(&ctx, buf, bytes);
	opt_SHA512t256_Final(&ctx, cksum_buf);
}

void
opt_SHA512t256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t cksum[6][32];
	SHA512_Context ctx[4];
	int i, rem;
	uint64_t _bytes;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes <= BLKSZ * 2) {
		opt_SHA512t256_Init(&ctx[0]);
		opt_SHA512t256_Update(&ctx[0], buf, bytes);
		opt_SHA512t256_Final(&ctx[0], cksum_buf);
		return;
	}

	/*
	 * Do first level hashes in parallel.
	 */
	_bytes = (bytes / BLKSZ) * BLKSZ;
	rem = bytes - _bytes;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 4; ++i)
	{
		uint64_t byt;

		byt = i * BLKSZ;
		opt_SHA512t256_Init(&ctx[i]);
		while (byt < _bytes) {
			opt_SHA512t256_Update(&ctx[i], buf + byt, BLKSZ);
			byt += 4 * BLKSZ;
		}
		if (i>0)
			opt_SHA512t256_Final(&ctx[i], cksum[i]);
	}
	if (rem > 0) {
		opt_SHA512t256_Update(&ctx[0], buf + bytes - rem, rem);
	}
	opt_SHA512t256_Final(&ctx[0], cksum[0]);

	/*
	 * Second level hashes.
	 */
	opt_SHA512t256_Init(&ctx[0]);
	opt_SHA512t256_Init(&ctx[1]);
	opt_SHA512t256_Update(&ctx[0], &cksum[0], 2 * 32);
	opt_SHA512t256_Update(&ctx[1], &cksum[1], 2 * 32);
	opt_SHA512t256_Final(&ctx[0], cksum[4]);
	opt_SHA512t256_Final(&ctx[1], cksum[5]);

	/*
	 * Final hash.
	 */
	opt_SHA512t256_Init(&ctx[0]);
	opt_SHA512t256_Update(&ctx[0], &cksum[4], 2 * 32);
	opt_SHA512t256_Final(&ctx[0], cksum_buf);
}

void
opt_SHA512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	SHA512_Context ctx;

	opt_SHA512_Init(&ctx);
	opt_SHA512_Update(&ctx, buf, bytes);
	opt_SHA512_Final(&ctx, cksum_buf);
}

void
opt_SHA512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t cksum[6][64];
	SHA512_Context ctx[4];
	int i, rem;
	uint64_t _bytes;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes <= BLKSZ * 2) {
		opt_SHA512_Init(&ctx[0]);
		opt_SHA512_Update(&ctx[0], buf, bytes);
		opt_SHA512_Final(&ctx[0], cksum_buf);
		return;
	}

	/*
	 * Do first level hashes in parallel.
	 */
	_bytes = (bytes / BLKSZ) * BLKSZ;
	rem = bytes - _bytes;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 4; ++i)
	{
		uint64_t byt;

		byt = i * BLKSZ;
		opt_SHA512_Init(&ctx[i]);
		while (byt < _bytes) {
			opt_SHA512_Update(&ctx[i], buf + byt, BLKSZ);
			byt += 4 * BLKSZ;
		}
		if (i>0)
			opt_SHA512_Final(&ctx[i], cksum[i]);
	}
	if (rem > 0) {
		opt_SHA512_Update(&ctx[0], buf + bytes - rem, rem);
	}
	opt_SHA512_Final(&ctx[0], cksum[0]);

	/*
	 * Second level hashes.
	 */
	opt_SHA512_Init(&ctx[0]);
	opt_SHA512_Init(&ctx[1]);
	opt_SHA512_Update(&ctx[0], &cksum[0], 2 * 64);
	opt_SHA512_Update(&ctx[1], &cksum[1], 2 * 64);
	opt_SHA512_Final(&ctx[0], cksum[4]);
	opt_SHA512_Final(&ctx[1], cksum[5]);

	/*
	 * Final hash.
	 */
	opt_SHA512_Init(&ctx[0]);
	opt_SHA512_Update(&ctx[0], &cksum[4], 2 * 64);
	opt_SHA512_Final(&ctx[0], cksum_buf);
}
