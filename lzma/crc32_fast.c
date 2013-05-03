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

///////////////////////////////////////////////////////////////////////////////
//
/// \file       crc32.c
/// \brief      CRC32 calculation
///
/// Calculate the CRC32 using the slice-by-eight algorithm.
/// It is explained in this document:
/// http://www.intel.com/technology/comms/perfnet/download/CRC_generators.pdf
/// The code in this file is not the same as in Intel's paper, but
/// the basic principle is identical.
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include <lzma_crc.h>

extern const uint32_t lzma_crc32_table[8][256];

// If you make any changes, do some bench marking! Seemingly unrelated
// changes can very easily ruin the performance (and very probably is
// very compiler dependent).
extern uint32_t
lzma_crc32(const uint8_t *buf, size_t size, uint32_t crc)
{
	crc = ~crc;

#ifdef WORDS_BIGENDIAN
	crc = bswap32(crc);
#endif

	if (size > 8) {
		// Fix the alignment, if needed. The if statement above
		// ensures that this won't read past the end of buf[].
		while ((uintptr_t)(buf) & 7) {
			crc = lzma_crc32_table[0][*buf++ ^ A(crc)] ^ S8(crc);
			--size;
		}

		// Calculate the position where to stop.
		const uint8_t *const limit = buf + (size & ~(size_t)(7));

		// Calculate how many bytes must be calculated separately
		// before returning the result.
		size &= (size_t)(7);

		// Calculate the CRC32 using the slice-by-eight algorithm.
		while (buf < limit) {
			crc ^= *(const uint32_t *)(buf);
			buf += 4;

			crc = lzma_crc32_table[7][A(crc)]
			    ^ lzma_crc32_table[6][B(crc)]
			    ^ lzma_crc32_table[5][C(crc)]
			    ^ lzma_crc32_table[4][D(crc)];

			const uint32_t tmp = *(const uint32_t *)(buf);
			buf += 4;

			// At least with some compilers, it is critical for
			// performance, that the crc variable is XORed
			// between the two table-lookup pairs.
			crc = lzma_crc32_table[3][A(tmp)]
			    ^ lzma_crc32_table[2][B(tmp)]
			    ^ crc
			    ^ lzma_crc32_table[1][C(tmp)]
			    ^ lzma_crc32_table[0][D(tmp)];
		}
	}

	while (size-- != 0)
		crc = lzma_crc32_table[0][*buf++ ^ A(crc)] ^ S8(crc);

#ifdef WORDS_BIGENDIAN
	crc = bswap32(crc);
#endif

	return ~crc;
}
