///////////////////////////////////////////////////////////////////////////////
//
/// \file       crc64.c
/// \brief      CRC64 calculation
///
/// Calculate the CRC64 using the slice-by-four algorithm. This is the same
/// idea that is used in crc32_fast.c, but for CRC64 we use only four tables
/// instead of eight to avoid increasing CPU cache usage.
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include <crc_macros.h>

#ifdef WORDS_BIGENDIAN
#	define A1(x) ((x) >> 56)
#else
#	define A1 A
#endif

extern const uint64_t lzma_crc64_table[4][256];

// See the comments in crc32_fast.c. They aren't duplicated here.
extern uint64_t
lzma_crc64(const uint8_t *buf, size_t size, uint64_t crc)
{
	crc = ~crc;

#ifdef WORDS_BIGENDIAN
	crc = bswap64(crc);
#endif

	if (size > 4) {
		while ((uintptr_t)(buf) & 3) {
			crc = lzma_crc64_table[0][*buf++ ^ A1(crc)] ^ S8(crc);
			--size;
		}

		const uint8_t *const limit = buf + (size & ~(size_t)(3));
		size &= (size_t)(3);

		while (buf < limit) {
#ifdef WORDS_BIGENDIAN
			const uint32_t tmp = (crc >> 32)
					^ *(const uint32_t *)(buf);
#else
			const uint32_t tmp = crc ^ *(const uint32_t *)(buf);
#endif
			buf += 4;

			crc = lzma_crc64_table[3][A(tmp)]
			    ^ lzma_crc64_table[2][B(tmp)]
			    ^ S32(crc)
			    ^ lzma_crc64_table[1][C(tmp)]
			    ^ lzma_crc64_table[0][D(tmp)];
		}
	}

	while (size-- != 0)
		crc = lzma_crc64_table[0][*buf++ ^ A1(crc)] ^ S8(crc);

#ifdef WORDS_BIGENDIAN
	crc = bswap64(crc);
#endif

	return ~crc;
}

extern uint64_t
lzma_crc64_8bchk(const uint8_t *buf, size_t size, uint64_t crc, uint64_t *cnt)
{
	crc = ~crc;
	uint64_t cnt1 = 0;

#ifdef WORDS_BIGENDIAN
	crc = bswap64(crc);
#endif

	if (size > 4) {
		while ((uintptr_t)(buf) & 3) {
			crc = lzma_crc64_table[0][*buf++ ^ A1(crc)] ^ S8(crc);
			--size;
		}

		const uint8_t *const limit = buf + (size & ~(size_t)(3));
		size &= (size_t)(3);

		while (buf < limit) {
			uint32_t val = *(const uint32_t *)(buf);
#ifdef WORDS_BIGENDIAN
			const uint32_t tmp = (crc >> 32) ^ val;
#else
			const uint32_t tmp = crc ^ val;
#endif
			buf += 4;
			val >>= 7; cnt1 += (val & 1);
			val >>= 8; cnt1 += (val & 1);

			crc = lzma_crc64_table[3][A(tmp)]
			    ^ lzma_crc64_table[2][B(tmp)]
			    ^ S32(crc)
			    ^ lzma_crc64_table[1][C(tmp)]
			    ^ lzma_crc64_table[0][D(tmp)];
		}
	}

	while (size-- != 0)
		crc = lzma_crc64_table[0][*buf++ ^ A1(crc)] ^ S8(crc);

#ifdef WORDS_BIGENDIAN
	crc = bswap64(crc);
#endif

	*cnt = cnt1;
	return ~crc;
}
