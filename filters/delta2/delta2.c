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

/*
 * These routines perform a kind of Adaptive Delta Encoding.
 * Initially the buffer is scanned to identify spans of values that
 * are monotonically increasing in arithmetic progression. These
 * values are not single bytes but consists of a stride of bytes
 * packed into an integer representation. Multiple stride lengths
 * (3, 5, 7, 8) are tried to find the one that gives the maximum
 * reduction. A span length threshold in bytes is used. Byte spans
 * less than this threshold are ignored.
 * Bytes are packed into integers in little-endian format.
 *
 * After an optimal stride length has been identified the encoder
 * performs a delta run length encoding on the spans. Two types of
 * objects are output by the encoder:
 * 1) A literal run of unmodified bytes. Header:
 *    64-bit encoded value of the following format
 *    Most Significant Byte = 0
 *    Remaining Bytes       = Length of literal span in bytes
 * 2) An encoded run length of a series in arithmetic progression.
 *    Header: 64bit encoded value
 *            64bit starting value of series
 *            64bit delta value
 *    64-bit encoded value is of the following format
 *    Most Significant Byte = Stride length
 *    Remaining Bytes       = Number of bytes in the span
 * 
 * We optimize for little-endian, so values are stored and interpreted
 * in little-endian order.
 */

#include <stdio.h>
#include <string.h>
#include <utils.h>
#include <transpose.h>
#include "delta2.h"

// Size of original data. 64 bits.
#define	MAIN_HDR	(sizeof (uint64_t))

// Literal text header block:
// 64bit encoded value.
#define	LIT_HDR		(sizeof (uint64_t))

// Delta encoded header block:
// 64bit encoded value
// 64bit initial value
// 64bit delta value
#define	DELTA_HDR	((sizeof (uint64_t)) * 3)

// Minimum span length
#define	MIN_THRESH	(50)
// Maximum data length (16TB)
#define	MAX_THRESH	(0x100000000000ULL)
#define	MSB_SETZERO_MASK	(0xffffffffffffffULL)
#define	MSB_SHIFT	(56)

/*
 * Delta2 algorithm processes data in blocks. The 4K size below is somewhat
 * adhoc but a couple of considerations were looked at:
 * 1) Balance between number of headers and delta runs. Too small chunks
 *    will increase header counts for long delta runs spanning chunks.
 *    Too large chunks will reduce effectiveness of locating more data
 *    tables.
 * 2) Chunk size should ideally be small enough to fit into L1 cache.
 */
#define	DELTA2_CHUNK	(4096)

/*
 * Stride values to be checked. As of this implementation strides only
 * upto 8 bytes (uint64_t) are supported.
 */
#define	NSTRIDES		NSTRIDES_EXTRA
static uchar_t strides[NSTRIDES] = {2, 4, 8, 3, 5, 6, 7};


static int delta2_encode_real(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen,
		int rle_thresh, int last_encode, int *hdr_ovr, int nstrides);

/*
 * Perform Delta2 encoding of the given data buffer in src. Delta Encoding
 * processes data in blocks of 4k. After each call to delta2_encode_real()
 * determine if any encoding was done.
 * If no delta sequence was found in the block then it is added to the running
 * literal span count. If delta was found in the block then the previous literal
 * span is copied out. If copying a literal span would overflow the destination
 * buffer then delta encoding is aborted.
 */
int
delta2_encode(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen, int rle_thresh, int nstrides)
{
	if (srclen > MAX_THRESH) {
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: srclen: %" PRIu64 " is too big.\n", srclen));
		return (-1);
	}

	if (nstrides > NSTRIDES) return (-1);
	if (srclen <= (MAIN_HDR + LIT_HDR + DELTA_HDR))
		return (-1);

	if (rle_thresh < MIN_THRESH)
		return (-1);

	if (*dstlen < DELTA2_CHUNK) {
		int hdr_ovr;
		int rv;

		hdr_ovr = 0;
		U64_P(dst) = LE64(srclen);
		dst += MAIN_HDR;
		rv = delta2_encode_real(src, srclen, dst, dstlen, rle_thresh, 1, &hdr_ovr, nstrides);
		if (rv == -1)
			return (rv);
		*dstlen += MAIN_HDR;
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: srclen: %" PRIu64 ", dstlen: %" PRIu64 "\n", srclen, *dstlen));
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: header overhead: %d\n", hdr_ovr));
	} else {
		uchar_t *srcpos, *dstpos, *lastdst, *lastsrc, *dstend;
		uint64_t slen, sz, dsz, pending;
		int rem, lenc, hdr_ovr;
		DEBUG_STAT_EN(double strt, en);
	
		srcpos = src;
		dstpos = dst;
		dstend = dst + *dstlen;
		slen = srclen;
		pending = 0;
		U64_P(dstpos) = LE64(srclen);
		dstpos += MAIN_HDR;
		lastdst = dstpos;
		lastsrc = srcpos;
		hdr_ovr = 0;

		DEBUG_STAT_EN(strt = get_wtime_millis());
		while (slen > 0) {
			if (slen > DELTA2_CHUNK) {
				sz = DELTA2_CHUNK;
				lenc = 0;
			} else {
				sz = slen;
				lenc = 1;
			}
			dsz = sz;
			rem = delta2_encode_real(srcpos, sz, dstpos, &dsz, rle_thresh, lenc,
						 &hdr_ovr, nstrides);
			if (rem == -1) {
				if (pending == 0) {
					lastdst = dstpos;
					lastsrc = srcpos;
					dstpos += LIT_HDR;
				}
				pending += dsz;
				srcpos += dsz;
				dstpos += dsz;
				slen -= dsz;
			} else {
				if (pending) {
					pending &=  MSB_SETZERO_MASK;
					U64_P(lastdst) = LE64(pending);
					lastdst += sizeof (uint64_t);
					memcpy(lastdst, lastsrc, pending);
					pending = 0;
				}
				srcpos += (sz - rem);
				slen -= (sz - rem);
				dstpos += dsz;
				if (dstpos > dstend) {
					DEBUG_STAT_EN(fprintf(stderr, "No Delta\n"));
					return (-1);
				}
			}
		}
		if (pending) {
			pending &=  MSB_SETZERO_MASK;
			U64_P(lastdst) = LE64(pending);
			lastdst += sizeof (uint64_t);
			if (lastdst + pending > dstend) {
				DEBUG_STAT_EN(fprintf(stderr, "No Delta\n"));
				return (-1);
			}
			memcpy(lastdst, lastsrc, pending);
		}
		*dstlen = dstpos - dst;
		DEBUG_STAT_EN(en = get_wtime_millis());
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: srclen: %" PRIu64 ", dstlen: %" PRIu64 "\n", srclen, *dstlen));
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: header overhead: %d\n", hdr_ovr));
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: Processed at %.3f MB/s\n", get_mb_s(srclen, strt, en)));
	}
	return (0);
}

/*
 * Process one block of data upto 4K in size.
 */
static int
delta2_encode_real(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen,
		   int rle_thresh, int last_encode, int *hdr_ovr, int nstrides)
{
	uint64_t snum, gtot1, gtot2, tot;
	uint64_t cnt, val, sval;
	uint64_t vl1, vl2, vld1, vld2;
	uchar_t *pos, *pos2, stride, st1;
	int st;

	assert(srclen == *dstlen);
	gtot1 = ULL_MAX;
	stride = 0;
	tot = 0;

	/*
	 * Estimate which stride length gives the max reduction given rle_thresh.
	 */
	for (st = 0; st < nstrides; st++) {
		int gt;

		snum = 0;
		gtot2 = LIT_HDR;
		vl1 = 0;
		vld1 = 0;
		tot = 0;
		pos = src;
		st1 = strides[st];
		sval = st1;
		sval = ((sval << 3) - 1);
		sval = (1ULL << sval);
		sval |= (sval - 1);
		val = 0;
		for (cnt = 0; cnt < (srclen - sizeof (cnt)); cnt += st1) {
			vl2 = U64_P(pos);
			vl2 = LE64(vl2);
			vl2 &= sval;
			vld2 = vl2 - vl1;
			if (vld1 != vld2) {
				if (snum > rle_thresh) {
					gt = (tot > 0);
					gtot2 += (LIT_HDR * gt);
					tot = 0;
					gtot2 += DELTA_HDR;
				} else {
					gtot2 += snum;
					tot += snum;
				}
				snum = 0;
			}
			snum += st1;
			vld1 = vld2;
			vl1 = vl2;
			pos += st1;
		}
		if (snum > rle_thresh) {
			gtot2 += DELTA_HDR;
			/*
			 * If this ended into another table reset next scan
			 * point to beginning of the table.
			 */
			val = cnt - snum;
		} else {
			gtot2 += snum;
			/*
			 * If this ended into another table reset next scan
			 * point to beginning of the table.
			 */
			if (snum >= (MIN_THRESH>>1))
				val = cnt - snum;
		}
		if (gtot2 < gtot1) {
			gtot1 = gtot2;
			stride = st1;
			tot = val;
		}
	}

	/*
	 * No need to check for destination buffer overflow since
	 * dstlen >= srclen always.
	 */
	if ( gtot1 > (srclen - (DELTA_HDR + LIT_HDR + MAIN_HDR)) ) {
		if (srclen == DELTA2_CHUNK) {
			if (tot > 0)
				*dstlen = tot;
		}
		return (-1);
	}

	/*
	 * Now perform encoding using the stride length.
	 */
	snum = 0;
	vl1 = 0;
	vld1 = 0;
	gtot1 = 0;
	pos = src;
	pos2 = dst;

	vl2 = U64_P(pos);
	vl2 = LE64(vl2);
	val = stride;
	val = ((val << 3) - 1);
	val = (1ULL << val);
	val |= (val - 1);
	vl2 &= val;
	sval = vl2;

	for (cnt = 0; cnt < (srclen - sizeof (cnt)); cnt += stride) {
		vl2 = U64_P(pos);
		vl2 = LE64(vl2);
		vl2 &= val;
		vld2 = vl2 - vl1;
		if (vld1 != vld2) {
			if (snum > rle_thresh) {
				/*
				 * We have a series but there is some pending literal data
				 * to be copied before the series begins. First copy that
				 * as a literal sequence.
				 */
				if (gtot1 > 0) {
					gtot1 &= MSB_SETZERO_MASK;
					U64_P(pos2) = LE64(gtot1);
					pos2 += sizeof (uint64_t);
					DEBUG_STAT_EN(*hdr_ovr += LIT_HDR);
					memcpy(pos2, pos - (gtot1+snum), gtot1);
					pos2 += gtot1;
					gtot1 = 0;
				}

				/*
				 * RLE Encode delta series. Store total number of bytes,
				 * stride length, starting value and difference between
				 * the terms.
				 */
				gtot2 = stride;
				gtot2 <<= MSB_SHIFT;
				gtot2 |= (snum & MSB_SETZERO_MASK);
				U64_P(pos2) = LE64(gtot2);
				pos2 += sizeof (uint64_t);
				U64_P(pos2) = LE64(sval);
				pos2 += sizeof (uint64_t);
				U64_P(pos2) = LE64(vld1);
				pos2 += sizeof (uint64_t);
				DEBUG_STAT_EN(*hdr_ovr += DELTA_HDR);
			} else {
				gtot1 += snum;
			}
			snum = 0;
			sval = vl2;
		}
		snum += stride;
		vld1 = vld2;
		vl1 = vl2;
		pos += stride;
	}

	/*
	 * Encode final sequence, if any.
	 */
	if (snum > 0) {
		if (snum > rle_thresh) {
			if (gtot1 > 0) {
				gtot1 &= MSB_SETZERO_MASK;
				U64_P(pos2) = LE64(gtot1);
				pos2 += sizeof (uint64_t);
				DEBUG_STAT_EN(*hdr_ovr += LIT_HDR);
				memcpy(pos2, pos - (gtot1+snum), gtot1);
				pos2 += gtot1;
				gtot1 = 0;
			}
			gtot2 = stride;
			gtot2 <<= MSB_SHIFT;
			gtot2 |= (snum & MSB_SETZERO_MASK);
			U64_P(pos2) = LE64(gtot2);
			pos2 += sizeof (uint64_t);
			U64_P(pos2) = LE64(sval);
			pos2 += sizeof (uint64_t);
			U64_P(pos2) = LE64(vld1);
			pos2 += sizeof (uint64_t);
			DEBUG_STAT_EN(*hdr_ovr += DELTA_HDR);

		} else if (last_encode) {
			gtot1 += snum;
			gtot1 &= MSB_SETZERO_MASK;
			U64_P(pos2) = LE64(gtot1);
			pos2 += sizeof (uint64_t);
			DEBUG_STAT_EN(*hdr_ovr += LIT_HDR);
			memcpy(pos2, pos - gtot1, gtot1);
			pos2 += gtot1;
		} else {
			gtot1 += snum;
		}
	}

	if (last_encode) {
		val = srclen - (pos - src);
		if (val > 0) {
			/*
			* Encode left over bytes, if any, at the end into a
			* literal run.
			*/
			val &= MSB_SETZERO_MASK;
			U64_P(pos2) = LE64(val);
			pos2 += sizeof (uint64_t);
			for (cnt = 0; cnt < val; cnt++) {
				*pos2 = *pos;
				++pos2; ++pos;
			}
			DEBUG_STAT_EN(*hdr_ovr += LIT_HDR);
		}
		val = 0;
	} else {
		val = gtot1 + (srclen - (pos - src));
	}
	*dstlen = pos2 - dst;
	return (val);
}

int
delta2_decode(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen)
{
	uchar_t *pos, *pos1, *last;
	uint64_t olen, val, sval, delta, rcnt, cnt, out, vl;
	uchar_t stride, flags;
	DEBUG_STAT_EN(double strt, en);

	pos = src;
	pos1 = dst;

	DEBUG_STAT_EN(strt = get_wtime_millis());
	last = pos + srclen;
	olen = LE64(U64_P(pos));
	if (*dstlen < olen) {
		log_msg(LOG_ERR, 0, "DELTA2 Decode: Destination buffer too small.\n");
		return (-1);
	}

	out = 0;
	pos += MAIN_HDR;

	while (pos < last) {
		val = U64_P(pos);
		val = LE64(val);
		flags = (val >> MSB_SHIFT) & 0xff;

		if (flags == 0) {
			/*
			 * Copy over literal run of bytes.
			 */
			rcnt = val & MSB_SETZERO_MASK;
			pos += sizeof (rcnt);
			if (out + rcnt > *dstlen) {
				log_msg(LOG_ERR, 0, "DELTA2 Decode(lit): Destination buffer overflow. Corrupt data.\n");
				return (-1);
			}
			memcpy(pos1, pos, rcnt);
			pos += rcnt;
			pos1 += rcnt;
			out += rcnt;

		} else {
			stride = flags;
			if (stride > STRIDE_MAX || stride < STRIDE_MIN) {
				log_msg(LOG_ERR, 0, "DELTA2 Decode(delta): Invalid stride length: %d. Corrupt data.\n", stride);
				return (-1);
			}
			rcnt = val & MSB_SETZERO_MASK;
			pos += sizeof (rcnt);
			sval = LE64(U64_P(pos));
			pos += sizeof (sval);
			delta = LE64(U64_P(pos));
			pos += sizeof (delta);
			if (out + rcnt > *dstlen) {
				log_msg(LOG_ERR, 0, "DELTA2 Decode(delta): Destination buffer overflow. Corrupt data.\n");
				return (-1);
			}

			vl = stride;
			vl = (vl << 3) - 1;
			vl = (1ULL << vl);
			vl |= (vl - 1);

			/*
			 * Recover original bytes from the arithmetic series using
			 * length, starting value and delta.
			 */
			for (cnt = 0; cnt < rcnt/stride; cnt++) {
				val = (sval & vl);
				U64_P(pos1) = LE64(val);
				out += stride;
				sval += delta;
				pos1 += stride;
			}
		}
	}
	*dstlen = out;
	DEBUG_STAT_EN(en = get_wtime_millis());
	DEBUG_STAT_EN(fprintf(stderr, "DELTA2: Decoded at %.3f MB/s\n", get_mb_s(out, strt, en)));
	return (0);
}
