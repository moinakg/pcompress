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

/*
 * Helper functions for single-call SHA3 (Keccak) hashing. Both serial
 * and parallel versions are provided.
 */

int
Keccak256_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t *pos[2];
	uint64_t len[2];
	uchar_t cksum[2][32];
	int i, rv[2];

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes / 2 <= KECCAK_BLOCK_SIZE * 2) {
		return (Keccak_Hash(256, buf, bytes * 8, cksum_buf));
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
		rv[i] = Keccak_Hash(256, pos[i], len[i] * 8, cksum[i]);
	}
	if (rv[0] != 0 || rv[1] != 0)
		return (-1);
	return (Keccak_Hash(256, (const BitSequence *)cksum, 2 * 32 * 8, cksum_buf));
}

int
Keccak512_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes)
{
	uchar_t *pos[2];
	uint64_t len[2];
	uchar_t cksum[2][64];
	int i, rv[2];

	/*
	 * Is it worth doing the overhead of parallelism ? Buffer large enough ?
	 * If not then just do a simple serial hashing.
	 */
	if (bytes / 2 <= KECCAK_BLOCK_SIZE * 2) {
		return (Keccak_Hash(512, buf, bytes * 8, cksum_buf));
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
		rv[i] = Keccak_Hash(512, pos[i], len[i] * 8, cksum[i]);
	}
	if (rv[0] != 0 || rv[1] != 0)
		return (-1);
	return (Keccak_Hash(512, (const BitSequence *)cksum, 2 * 64 * 8, cksum_buf));
}
