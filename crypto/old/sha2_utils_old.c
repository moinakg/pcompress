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

void
ossl_SHA256_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t *pos[2];
	uint64_t len[2];
	uchar_t cksum[2][32];
	int i;
	SHA256_CTX *mctx;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes / 2 <= SHA512_BLOCK_SIZE * 4) {
		mctx = (SHA256_CTX *)malloc(sizeof (SHA256_CTX));
		SHA256_Init(mctx);
		SHA256_Update(mctx, buf, bytes);
		SHA256_Final(cksum_buf, mctx);
		free(mctx);
		return;
	}
	pos[0] = buf;
	len[0] = bytes/2;
	buf += bytes/2;
	pos[1] = buf;
	len[1] = bytes - bytes/2;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 2; ++i)
	{
		SHA256_CTX ctx;
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, pos[i], len[i]);
		SHA256_Final(cksum[i], &ctx);
	}
	mctx = (SHA256_CTX *)malloc(sizeof (SHA256_CTX));
	SHA256_Init(mctx);
	SHA256_Update(mctx, cksum, 2 * 32);
	SHA256_Final(cksum_buf, mctx);
	free(mctx);
}

void
ossl_SHA512_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t *pos[2];
	uint64_t len[2];
	uchar_t cksum[2][64];
	int i;
	SHA512_CTX *mctx;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple hashing.
	 */
	if (bytes / 2 <= SHA512_BLOCK_SIZE * 4) {
		mctx = (SHA512_CTX *)malloc(sizeof (SHA512_CTX));
		SHA512_Init(mctx);
		SHA512_Update(mctx, buf, bytes);
		SHA512_Final(cksum_buf, mctx);
		free(mctx);
		return;
	}
	pos[0] = buf;
	len[0] = bytes/2;
	pos[1] = buf + bytes/2;
	len[1] = bytes - bytes/2;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 2; ++i)
	{
		SHA512_CTX ctx;
		SHA512_Init(&ctx);
		SHA512_Update(&ctx, pos[i], len[i]);
		SHA512_Final(cksum[i], &ctx);
	}
	mctx = (SHA512_CTX *)malloc(sizeof (SHA512_CTX));
	SHA512_Init(mctx);
	SHA512_Update(mctx, cksum, 2 * 64);
	SHA512_Final(cksum_buf, mctx);
	free(mctx);
}

void
opt_SHA512t256_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t *pos[2];
	uint64_t len[2];
	uchar_t cksum[2][32];
	int i;
	SHA512_Context *mctx;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes / 2 <= SHA512_BLOCK_SIZE * 4) {
		mctx = (SHA512_Context *)malloc(sizeof (SHA512_Context));
		opt_SHA512t256_Init(mctx);
		opt_SHA512t256_Update(mctx, buf, bytes);
		opt_SHA512t256_Final(mctx, cksum_buf);
		free(mctx);
		return;
	}
	pos[0] = buf;
	len[0] = bytes/2;
	pos[1] = buf + bytes/2;
	len[1] = bytes - bytes/2;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 2; ++i)
	{
		SHA512_Context ctx;
		opt_SHA512t256_Init(&ctx);
		opt_SHA512t256_Update(&ctx, pos[i], len[i]);
		opt_SHA512t256_Final(&ctx, cksum[i]);
	}
	mctx = (SHA512_Context *)malloc(sizeof (SHA512_Context));
	opt_SHA512t256_Init(mctx);
	opt_SHA512t256_Update(mctx, cksum, 2 * 32);
	opt_SHA512t256_Final(mctx, cksum_buf);
	free(mctx);
}

void
opt_SHA512_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t *pos[2];
	uint64_t len[2];
	uchar_t cksum[2][64];
	int i;
	SHA512_Context *mctx;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes / 2 <= SHA512_BLOCK_SIZE * 4) {
		mctx = (SHA512_Context *)malloc(sizeof (SHA512_Context));
		opt_SHA512_Init(mctx);
		opt_SHA512_Update(mctx, buf, bytes);
		opt_SHA512_Final(mctx, cksum_buf);
		free(mctx);
		return;
	}
	pos[0] = buf;
	len[0] = bytes/2;
	pos[1] = buf + bytes/2;
	len[1] = bytes - bytes/2;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 2; ++i)
	{
		SHA512_Context ctx;
		opt_SHA512_Init(&ctx);
		opt_SHA512_Update(&ctx, pos[i], len[i]);
		opt_SHA512_Final(&ctx, cksum[i]);
	}
	mctx = (SHA512_Context *)malloc(sizeof (SHA512_Context));
	opt_SHA512_Init(mctx);
	opt_SHA512_Update(mctx, cksum, 2 * 64);
	opt_SHA512_Final(mctx, cksum_buf);
	free(mctx);
}

