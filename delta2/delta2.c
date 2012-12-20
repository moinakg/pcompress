/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012 Moinak Ghosh. All rights reserved.
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
 * Bytes are packed into integers in big-endian format.
 *
 * After an optimal stride length has been identified the encoder
 * performs a delta run length encoding on the spans. Three types of
 * objects are output by the encoder:
 * 1) A literal run of unmodified bytes. Header: 1 zero byte followed
 *    by a 64bit length in bytes.
 * 2) A literal run of transposed bytes containing sequences that are
 *    below threshold and the total span of those sequences is at least
 *    87% of the entire run.
 *    Header: 1 byte stride length with high bit set.
 *            64bit length of span in bytes.
 * 3) An encoded run length of a series in arithmetic progression.
 *    Header: 1 byte stride length (must be less than 128)
 *            64bit length of span in bytes
 *            64bit starting value of series
 *            64bit delta value
 */
#include <stdio.h>
#include <string.h>
#include <utils.h>
#include <transpose.h>
#include "delta2.h"

// Size of original data. 64 bits.
#define	MAIN_HDR	(sizeof (uint64_t))

// Literal text header block:
// 1-byte flag
// 64bit length of run in bytes.
#define	LIT_HDR		(1 + sizeof (uint64_t))
#define	TRANSP_HDR	(LIT_HDR)

// Delta encoded header block:
// 1-byte flag indicating stride length
// 64bit length of span in bytes
// 64bit initial value
// 64bit delta value
#define	DELTA_HDR	(1 + (sizeof (uint64_t)) * 3)

// Minimum span length
#define	MIN_THRESH	(50)
#define	TRANSP_THRESH	(100)
#define	TRANSP_BIT	(128)
#define	TRANSP_MASK	(127)

/*
 * Delta2 algorithm processes data in chunks. The 4K size below is somewhat
 * adhoc but a couple of considerations were looked at:
 * 1) Balance between number of headers and delta runs. Too small chunks
 *    will increase header counts for long delta runs spanning chunks.
 *    Too large chunks will reduce effectiveness of locating more data
 *    tables.
 * 2) Chunk size should ideally be small enough to fit into L1 cache.
 */
#define	DELTA2_CHUNK	(4096)

static int delta2_encode_real(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen,
		int rle_thresh, int last_encode, int *transp_count, int *hdr_ovr);

int
delta2_encode(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen, int rle_thresh)
{
	if (*dstlen < DELTA2_CHUNK) {
		int transp_count, hdr_ovr;
		int rv;

		transp_count = 0;
		hdr_ovr = 0;
		rv = delta2_encode_real(src, srclen, dst, dstlen, rle_thresh, 1, &transp_count, &hdr_ovr);
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: srclen: %" PRIu64 ", dstlen: %" PRIu64 "\n", srclen, *dstlen));
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: transpositions: %d, header overhead: %d\n", transp_count, hdr_ovr));
	} else {
		uchar_t *srcpos, *dstpos, *lastdst, *lastsrc, *dstend;
		uint64_t slen, sz, dsz, pending;
		int rem, lenc, transp_count, hdr_ovr;

		srcpos = src;
		dstpos = dst;
		dstend = dst + *dstlen;
		slen = srclen;
		pending = 0;
		lastdst = dst;
		lastsrc = src;
		*((uint64_t *)dstpos) = htonll(srclen);
		dstpos += MAIN_HDR;
		transp_count = 0;
		hdr_ovr = 0;

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
						 &transp_count, &hdr_ovr);
			if (rem == -1) {
				if (pending == 0) {
					lastdst = dstpos;
					lastsrc = srcpos;
					dstpos += LIT_HDR;
				}
				pending += sz;
				srcpos += sz;
				dstpos += sz;
				slen -= sz;
			} else {
				if (pending) {
					*lastdst = 0;
					lastdst++;
					*((uint64_t *)lastdst) = htonll(pending);
					lastdst += sizeof (uint64_t);
					memcpy(lastdst, lastsrc, pending);
					pending = 0;
				}
				srcpos += (sz - rem);
				slen -= (sz - rem);
				dstpos += dsz;
				if (dstpos > dstend) return (-1);
			}
		}
		if (pending) {
			*lastdst = 0;
			lastdst++;
			*((uint64_t *)lastdst) = htonll(pending);
			lastdst += sizeof (uint64_t);
			if (lastdst + pending > dstend) return (-1);
			memcpy(lastdst, lastsrc, pending);
		}
		*dstlen = dstpos - dst;
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: srclen: %" PRIu64 ", dstlen: %" PRIu64 "\n", srclen, *dstlen));
		DEBUG_STAT_EN(fprintf(stderr, "DELTA2: transpositions: %d, header overhead: %d\n", transp_count, hdr_ovr));
	}
	return (0);
}

static int
delta2_encode_real(uchar_t *src, uint64_t srclen, uchar_t *dst, uint64_t *dstlen,
		   int rle_thresh, int last_encode, int *transp_count, int *hdr_ovr)
{
	uint64_t snum, gtot1, gtot2, tot;
	uint64_t cnt, val, sval;
	uint64_t vl1, vl2, vld1, vld2;
	uchar_t *pos, *pos2, stride, st1;
	uchar_t strides[4] = {3, 5, 7, 8};
	int st, sz;

	if (rle_thresh < MIN_THRESH)
		return (-1);
	gtot1 = ULL_MAX;
	stride = 0;
	sz = sizeof (strides) / sizeof (strides[0]);

	/*
	 * Estimate which stride length gives the max reduction given rle_thresh.
	 */
	for (st = 0; st < sz; st++) {
		snum = 0;
		gtot2 = MAIN_HDR + LIT_HDR;
		vl1 = 0;
		vld1 = 0;
		tot = 0;
		pos = src;
		st1 = strides[st];
		for (cnt = 0; cnt < (srclen - sizeof (cnt)); cnt += st1) {
			vl2 = *((uint64_t *)pos);
			vl2 = htonll(vl2);
			vl2 >>= ((sizeof (vl2) - st1) << 3);
			vld2 = vl2 - vl1;
			if (vld1 != vld2) {
				if (snum > rle_thresh) {
					if (tot > 0) {
						gtot2 += LIT_HDR;
						tot = 0;
					}
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
		} else {
			gtot2 += snum;
		}
		if (gtot2 < gtot1) {
			gtot1 = gtot2;
			stride = st1;
		}
	}

	if (!(gtot1 < srclen && srclen - gtot1 > (DELTA_HDR + LIT_HDR + MAIN_HDR))) {
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
	gtot2 = 0;

	if (rle_thresh <= TRANSP_THRESH) {
		tot = rle_thresh/2;
	} else {
		tot = TRANSP_THRESH;
	}
	vl2 = *((uint64_t *)pos);
	vl2 = htonll(vl2);
	vl2 >>= ((sizeof (vl2) - stride) << 3);
	sval = vl2;

	for (cnt = 0; cnt < (srclen - sizeof (cnt)); cnt += stride) {
		val = *((uint64_t *)pos);
		vl2 = htonll(val);
		vl2 >>= ((sizeof (vl2) - stride) << 3);
		vld2 = vl2 - vl1;
		if (vld1 != vld2) {
			if (snum > rle_thresh) {
				if (gtot1 > 0) {
					/*
					 * Encode previous literal run, if any. If the literal run
					 * has enough (87%+) large sequences just below threshold,
					 * do a matrix transpose on the range in the hope of achieving
					 * a better compression ratio.
					 */
					if (gtot2 >= ((gtot1 >> 1) + (gtot1 >> 2) + (gtot1 >> 3))) {
						*pos2 = stride | TRANSP_BIT;
						pos2++;
						*((uint64_t *)pos2) = htonll(gtot1);
						pos2 += sizeof (uint64_t);
						DEBUG_STAT_EN((*transp_count)++);
						DEBUG_STAT_EN(*hdr_ovr += TRANSP_HDR);
						transpose(pos - (gtot1+snum), pos2, gtot1, stride, ROW);
					} else {
						*pos2 = 0;
						pos2++;
						*((uint64_t *)pos2) = htonll(gtot1);
						pos2 += sizeof (uint64_t);
						DEBUG_STAT_EN(*hdr_ovr += LIT_HDR);
						memcpy(pos2, pos - (gtot1+snum), gtot1);
					}
					pos2 += gtot1;
					gtot1 = 0;
					gtot2 = 0;
				}
				/*
				 * RLE Encode delta series.
				 */
				*pos2 = stride;
				pos2++;
				*((uint64_t *)pos2) = htonll(snum);
				pos2 += sizeof (uint64_t);
				*((uint64_t *)pos2) = htonll(sval);
				pos2 += sizeof (uint64_t);
				*((uint64_t *)pos2) = htonll(vld1);
				pos2 += sizeof (uint64_t);
				DEBUG_STAT_EN(*hdr_ovr += DELTA_HDR);
			} else {
				gtot1 += snum;
				if (snum >= tot)
					gtot2 += snum;
			}
			snum = 0;
			sval = vl2;
		}
		snum += stride;
		vld1 = vld2;
		vl1 = vl2;
		pos += stride;
	}

	if (snum > 0) {
		if (snum > rle_thresh) {
			if (gtot1 > 0) {
				*pos2 = 0;
				pos2++;
				*((uint64_t *)pos2) = htonll(gtot1);
				pos2 += sizeof (uint64_t);
				DEBUG_STAT_EN(*hdr_ovr += LIT_HDR);
				memcpy(pos2, pos - (gtot1+snum), gtot1);
				pos2 += gtot1;
				gtot1 = 0;
			}
			*pos2 = stride;
			pos2++;
			*((uint64_t *)pos2) = htonll(snum);
			pos2 += sizeof (uint64_t);
			*((uint64_t *)pos2) = htonll(sval);
			pos2 += sizeof (uint64_t);
			*((uint64_t *)pos2) = htonll(vld1);
			pos2 += sizeof (uint64_t);
			DEBUG_STAT_EN(*hdr_ovr += DELTA_HDR);

		} else if (last_encode) {
			gtot1 += snum;
			*pos2 = 0;
			pos2++;
			*((uint64_t *)pos2) = htonll(gtot1);
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
			*pos2 = 0;
			pos2++;
			*((uint64_t *)pos2) = htonll(val);
			pos2 += sizeof (uint64_t);
			for (cnt = 0; cnt < val; cnt++) {
				*pos2 = *pos;
				pos2++; pos++;
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
	uint64_t olen, val, sval, delta, rcnt, cnt, out;
	uchar_t stride;

	pos = src;
	pos1 = dst;

	last = pos + srclen;
	olen = ntohll(*((uint64_t *)pos));
	if (*dstlen < olen) {
		fprintf(stderr, "DELTA2 Decode: Destination buffer too small.\n");
		return (-1);
	}

	out = 0;
	pos += MAIN_HDR;

	while (pos < last) {
		if (*pos == 0) {
			/*
			 * Copy over literal run of bytes.
			 */
			pos++;
			rcnt = ntohll(*((uint64_t *)pos));
			pos += sizeof (rcnt);
			if (out + rcnt > *dstlen) {
				fprintf(stderr, "DELTA2 Decode: Destination buffer overflow. Corrupt data.\n");
				return (-1);
			}
			memcpy(pos1, pos, rcnt);
			pos += rcnt;
			pos1 += rcnt;
			out += rcnt;

		} else if (*pos & TRANSP_BIT) {
			int stride;
			/*
			 * Copy over literal run of transposed bytes and inverse-transpose.
			 */
			stride = (*pos & TRANSP_MASK);
			pos++;
			rcnt = ntohll(*((uint64_t *)pos));
			pos += sizeof (rcnt);
			if (out + rcnt > *dstlen) {
				fprintf(stderr, "DELTA2 Decode: Destination buffer overflow. Corrupt data.\n");
				return (-1);
			}
			transpose(pos, pos1, rcnt, stride, COL);
			pos += rcnt;
			pos1 += rcnt;
			out += rcnt;
		} else {
			stride = *pos;
			pos++;
			rcnt = ntohll(*((uint64_t *)pos));
			pos += sizeof (rcnt);
			sval = ntohll(*((uint64_t *)pos));
			pos += sizeof (sval);
			delta = ntohll(*((uint64_t *)pos));
			pos += sizeof (delta);
			if (out + rcnt > *dstlen) {
				fprintf(stderr, "DELTA2 Decode: Destination buffer overflow. Corrupt data.\n");
				return (-1);
			}

			/*
			 * Recover original bytes from the arithmetic series using
			 * length, starting value and delta.
			 */
			for (cnt = 0; cnt < rcnt/stride; cnt++) {
				val = sval << ((sizeof (val) - stride) << 3);
				*((uint64_t *)pos1) = ntohll(val);
				out += stride;
				sval += delta;
				pos1 += stride;
			}
		}
	}
	*dstlen = out;
	return (0);
}
