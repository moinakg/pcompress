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
#include <KeccakNISTInterface.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(_OPENMP)
#include <omp.h>
#endif
#include <utils.h>

#define	KECCAK_BLOCK_SIZE	1024
#define	BLKSZ			(2048)

/*
 * Helper functions for single-call SHA3 (Keccak) hashing. Both serial
 * and parallel versions are provided. Parallel versions use 2-stage
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

int
Keccak256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	return (Keccak_Hash(256, buf, bytes * 8, cksum_buf));
}

int
Keccak256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t cksum[6][32];
	hashState ctx[4];
	int i, rem, rv[4];
	uint64_t _bytes;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes <= BLKSZ) {
		return (Keccak_Hash(256, buf, bytes * 8, cksum_buf));
	}

	/*
	 * Do first level hashes in parallel.
	 */
	for (i = 0; i < 4; ++i) rv[i] = 0;
	_bytes = (bytes / BLKSZ) * BLKSZ;
	rem = bytes - _bytes;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 4; ++i)
	{
		uint64_t byt;

		byt = i * BLKSZ;
		rv[i] |= Keccak_Init(&ctx[i], 256);
		while (byt < _bytes) {
			rv[i] |= Keccak_Update(&ctx[i], buf + byt, BLKSZ * 8);
			byt += 4 * BLKSZ;
		}
		if (i>0)
			rv[i] |= Keccak_Final(&ctx[i], cksum[i]);
	}
	if (rem > 0) {
		rv[0] |= Keccak_Update(&ctx[0], buf + bytes - rem, rem * 8);
	}
	rv[0] |= Keccak_Final(&ctx[0], cksum[0]);

	for (i = 0; i < 4; ++i) if (rv[i] != 0) return (-1);
	rv[0] = 0;
	rv[1] = 0;

	/*
	 * Second level hashes.
	 */
	rv[0] |= Keccak_Init(&ctx[0], 256);
	rv[1] |= Keccak_Init(&ctx[1], 256);
	rv[0] |= Keccak_Update(&ctx[0], (const BitSequence *)&cksum[0], 2 * 32 * 8);
	rv[1] |= Keccak_Update(&ctx[1], (const BitSequence *)&cksum[1], 2 * 32 * 8);
	rv[0] |= Keccak_Final(&ctx[0], cksum[4]);
	rv[1] |= Keccak_Final(&ctx[1], cksum[5]);
	for (i = 0; i < 2; ++i) if (rv[i] != 0) return (-1);

	/*
	 * Final hash.
	 */
	rv[0] = 0;
	rv[0] |= Keccak_Init(&ctx[0], 256);
	rv[0] |= Keccak_Update(&ctx[0], (const BitSequence *)&cksum[4], 2 * 32 * 8);
	rv[0] |= Keccak_Final(&ctx[0], cksum_buf);
	return (rv[0]);
}

int
Keccak512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	return (Keccak_Hash(512, buf, bytes * 8, cksum_buf));
}

int
Keccak512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t cksum[6][64];
	hashState ctx[4];
	int i, rem, rv[4];
	uint64_t _bytes;

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes <= BLKSZ) {
		return (Keccak_Hash(512, buf, bytes * 8, cksum_buf));
	}

	/*
	 * Do first level hashes in parallel.
	 */
	for (i = 0; i < 4; ++i) rv[i] = 0;
	_bytes = (bytes / BLKSZ) * BLKSZ;
	rem = bytes - _bytes;
#if defined(_OPENMP)
#	pragma omp parallel for
#endif
	for(i = 0; i < 4; ++i)
	{
		uint64_t byt;

		byt = i * BLKSZ;
		rv[i] |= Keccak_Init(&ctx[i], 512);
		while (byt < _bytes) {
			rv[i] |= Keccak_Update(&ctx[i], buf + byt, BLKSZ * 8);
			byt += 4 * BLKSZ;
		}
		if (i>0)
			rv[i] |= Keccak_Final(&ctx[i], cksum[i]);
	}
	if (rem > 0) {
		rv[0] |= Keccak_Update(&ctx[0], buf + bytes - rem, rem * 8);
	}
	rv[0] |= Keccak_Final(&ctx[0], cksum[0]);

	for (i = 0; i < 4; ++i) if (rv[i] != 0) return (-1);
	rv[0] = 0;
	rv[1] = 0;

	/*
	 * Second level hashes.
	 */
	rv[0] |= Keccak_Init(&ctx[0], 512);
	rv[1] |= Keccak_Init(&ctx[1], 512);
	rv[0] |= Keccak_Update(&ctx[0], (const BitSequence *)&cksum[0], 2 * 64 * 8);
	rv[1] |= Keccak_Update(&ctx[1], (const BitSequence *)&cksum[1], 2 * 64 * 8);
	rv[0] |= Keccak_Final(&ctx[0], cksum[4]);
	rv[1] |= Keccak_Final(&ctx[1], cksum[5]);
	for (i = 0; i < 2; ++i) if (rv[i] != 0) return (-1);

	/*
	 * Final hash.
	 */
	rv[0] = 0;
	rv[0] |= Keccak_Init(&ctx[0], 512);
	rv[0] |= Keccak_Update(&ctx[0], (const BitSequence *)&cksum[4], 2 * 64 * 8);
	rv[0] |= Keccak_Final(&ctx[0], cksum_buf);
	return (rv[0]);
}
