/*
 * rabin_polynomial.c
 * 
 * Created by Joel Lawrence Tucci on 09-March-2011.
 * 
 * Copyright (c) 2011 Joel Lawrence Tucci
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the project's author nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

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
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <allocator.h>
#include <utils.h>

// CRC64 pieces from LZMA's implementation -----------------
#include <crc_macros.h>

#ifdef WORDS_BIGENDIAN
#	define A1(x) ((x) >> 56)
#else
#	define A1 A
#endif

extern const uint64_t lzma_crc64_table[4][256];
// ---------------------------------------------------------

#include "rabin_polynomial.h"

unsigned int rabin_polynomial_max_block_size = RAB_POLYNOMIAL_MAX_BLOCK_SIZE;
unsigned int rabin_polynomial_min_block_size = RAB_POLYNOMIAL_MIN_BLOCK_SIZE;
unsigned int rabin_avg_block_mask = RAB_POLYNOMIAL_AVG_BLOCK_MASK;

/*
 * Initialize the algorithm with the default params.
 */
rabin_context_t *
create_rabin_context(uint64_t chunksize) {
	rabin_context_t *ctx;
	unsigned char *current_window_data;
	unsigned int blknum;

	blknum = chunksize / rabin_polynomial_min_block_size;
	if (chunksize % rabin_polynomial_min_block_size)
		blknum++;

	ctx = (rabin_context_t *)slab_alloc(NULL, sizeof (rabin_context_t));
	current_window_data = slab_alloc(NULL, RAB_POLYNOMIAL_WIN_SIZE);
	ctx->blocks = (rabin_blockentry_t *)slab_alloc(NULL,
		blknum * rabin_polynomial_min_block_size);
	if(ctx == NULL || current_window_data == NULL || ctx->blocks == NULL) {
		fprintf(stderr,
		    "Could not allocate rabin polynomial context, out of memory\n");
		return (NULL);
	}

	/*
	 * We should compute the power for the window size.
	 * static uint64_t polynomial_pow;
	 * polynomial_pow = 1;
	 * for(index=0; index<RAB_POLYNOMIAL_WIN_SIZE; index++) {
	 *     polynomial_pow *= RAB_POLYNOMIAL_CONST;
	 * }
	 * But since RAB_POLYNOMIAL_CONST == 2, any expression of the form
	 * x * polynomial_pow can we written as x << RAB_POLYNOMIAL_WIN_SIZE
	 */

	ctx->current_window_data = current_window_data;
	reset_rabin_context(ctx);
	return (ctx);
}

void
reset_rabin_context(rabin_context_t *ctx)
{
	memset(ctx->current_window_data, 0, RAB_POLYNOMIAL_WIN_SIZE);
	ctx->window_pos = 0;
	ctx->cur_roll_checksum = 0;
	ctx->cur_checksum = 0;
}

void
destroy_rabin_context(rabin_context_t *ctx)
{
	slab_free(NULL, ctx->current_window_data);
	slab_free(NULL, ctx->blocks);
	slab_free(NULL, ctx);
}

/*
 * Checksum Comparator for qsort
 */
static int
cmpblks(const void *a, const void *b)
{
	rabin_blockentry_t *a1 = (rabin_blockentry_t *)a;
	rabin_blockentry_t *b1 = (rabin_blockentry_t *)b;

	if (a1->checksum < b1->checksum)
		return (-1);
	else if (a1->checksum == b1->checksum)
		return (0);
	else if (a1->checksum > b1->checksum)
		return (1);
}

/**
 * Perform Deduplication based on Rabin Fingerprinting. A 32-byte window is used for
 * the rolling checksum and dedup blocks vary in size from 4K-128K.
 */
void
rabin_dedup(rabin_context_t *ctx, uchar_t *buf, ssize_t *size, ssize_t offset)
{
	ssize_t i, last_offset;
	unsigned int blknum;
	char *buf1 = (char *)buf;
	unsigned int length;
	ssize_t overhead;

	length = offset;
	last_offset = 0;
	blknum = 0;
	ctx->valid = 0;

	if (*size < RAB_POLYNOMIAL_AVG_BLOCK_SIZE) return;
	for (i=offset; i<*size; i++) {
		char cur_byte = buf1[i];
		uint64_t pushed_out = ctx->current_window_data[ctx->window_pos];
		ctx->current_window_data[ctx->window_pos] = cur_byte;
		/*
		 * We want to do:
		 * cur_roll_checksum = cur_roll_checksum * RAB_POLYNOMIAL_CONST + cur_byte;
		 * cur_roll_checksum -= pushed_out * polynomial_pow;
		 * cur_checksum = cur_checksum * RAB_POLYNOMIAL_CONST + cur_byte;
		 *
		 * However since RAB_POLYNOMIAL_CONST == 2, we use shifts.
		 */
		ctx->cur_roll_checksum = (ctx->cur_roll_checksum << 1) + cur_byte;
		ctx->cur_roll_checksum -= (pushed_out << RAB_POLYNOMIAL_WIN_SIZE);
		// CRC64 Calculation swiped from LZMA
		ctx->cur_checksum = lzma_crc64_table[0][cur_byte ^ A1(ctx->cur_checksum)] ^ S8(ctx->cur_checksum);

		ctx->window_pos++;
		length++;

		if (ctx->window_pos == RAB_POLYNOMIAL_WIN_SIZE) // Loop back around
			ctx->window_pos=0;

		if (length < rabin_polynomial_min_block_size) continue;

		// If we hit our special value or reached the max block size update block offset
		if ((ctx->cur_roll_checksum & rabin_avg_block_mask) == RAB_POLYNOMIAL_CONST ||
		    length >= rabin_polynomial_max_block_size) {
			ctx->blocks[blknum].offset = last_offset;
			ctx->blocks[blknum].index = blknum; // Need to store for sorting
			ctx->blocks[blknum].checksum = ctx->cur_checksum;
			ctx->blocks[blknum].length = length;

			blknum++;
			ctx->cur_checksum = 0;
			last_offset = i+1;
			length = 0;
		}
	}

	// If we found at least a few chunks, perform dedup.
	if (blknum > 2) {
		uint64_t prev_cksum;
		unsigned int blk, prev_length;
		ssize_t pos, matches;
		int valid = 1;
		char *tmp, *prev_offset;
		unsigned int *blkarr, prev_blk;

		// Insert the last left-over trailing bytes, if any, into a block.
		if (last_offset < *size) {
			ctx->blocks[blknum].offset = last_offset;
			ctx->blocks[blknum].index = blknum;
			ctx->blocks[blknum].checksum = ctx->cur_checksum;
			ctx->blocks[blknum].length = *size - last_offset;
			blknum++;
			ctx->cur_checksum = 0;
			last_offset = *size;
		}

		overhead = blknum * RABIN_ENTRY_SIZE + RABIN_HDR_SIZE;
		prev_cksum = 0;
		prev_length = 0;
		prev_offset = 0;
		pos = overhead;

		/*
		 * Now sort the block array based on checksums. This will bring virtually 
		 * all similar block entries together. Effectiveness depends on how strong
		 * our checksum is. We are using CRC64 here so we should be pretty okay.
		 * TODO: Test with a heavily optimized MD5 (from OpenSSL?) later.
		 */
		qsort(ctx->blocks, blknum, sizeof (rabin_blockentry_t), cmpblks);
		blkarr = (unsigned int *)(ctx->cbuf + RABIN_HDR_SIZE);
		matches = 0;

		/*
		 * Now make a pass through the sorted block array making identical blocks
		 * point to the first identical block entry. A simple Run Length Encoding
		 * sort of. Checksums, length and contents (memcmp()) must match for blocks
		 * to be considered identical.
		 * The block index in the chunk is initialized with pointers into the
		 * sorted block array.
		 */
		for (blk = 0; blk < blknum; blk++) {
			blkarr[ctx->blocks[blk].index] = blk;

			if (blk > 0 && ctx->blocks[blk].checksum == prev_cksum &&
			    ctx->blocks[blk].length == prev_length &&
			    memcmp(prev_offset, buf1 + ctx->blocks[blk].offset, prev_length) == 0) {
				ctx->blocks[blk].length = 0;
				ctx->blocks[blk].index = prev_blk;
				matches += prev_length;
				continue;
			}

			prev_offset = buf1 + ctx->blocks[blk].offset;
			prev_cksum = ctx->blocks[blk].checksum;
			prev_length = ctx->blocks[blk].length;
			prev_blk = ctx->blocks[blk].index;
		}

		if (matches < overhead) {
			ctx->valid = 0;
			return;
		}
		/*
		 * Another pass, this time through the block index in the chunk. We insert
		 * block length into unique block entries. For block entries that are
		 * identical with another one we store the index number + max rabin block length.
		 * This way we can differentiate between a unique block length entry and a
		 * pointer to another block without needing a separate flag.
		 */
		for (blk = 0; blk < blknum; blk++) {
			rabin_blockentry_t *be;

			/*
			 * If blocks are overflowing the allowed chunk size then dedup did not
			 * help at all. We invalidate the dedup operation.
			 */
			if (pos > last_offset) {
				valid = 0;
				break;
			}
			be = &(ctx->blocks[blkarr[blk]]);
			if (be->length > 0) {
				prev_offset = buf1 + be->offset;
				memcpy(ctx->cbuf + pos, prev_offset, be->length);
				pos += be->length;
				blkarr[blk] = htonl(be->length);
			} else {
				blkarr[blk] = htonl(RAB_POLYNOMIAL_MAX_BLOCK_SIZE + be->index + 1);
			}
		}

cont:
		if (valid) {
			*((unsigned int *)(ctx->cbuf)) = htonl(blknum);
 			*((ssize_t *)(ctx->cbuf + sizeof (unsigned int))) = htonll(*size);
			*size = pos;
			ctx->valid = 1;
		}
	}
}

void
rabin_inverse_dedup(rabin_context_t *ctx, uchar_t *buf, ssize_t *size)
{
	unsigned int blknum, blk, oblk, len;
	unsigned int *blkarr;
	ssize_t orig_size, sz;
	ssize_t overhead, pos1, i;
	uchar_t *pos2;

	blknum = ntohl(*((unsigned int *)(buf)));
	orig_size = ntohll(*((ssize_t *)(buf + sizeof (unsigned int))));
	blkarr = (unsigned int *)(buf + RABIN_HDR_SIZE);
	overhead = blknum * RABIN_ENTRY_SIZE + RABIN_HDR_SIZE;
	pos1 = overhead;
	pos2 = ctx->cbuf;
	sz = 0;
	ctx->valid = 1;

	for (blk = 0; blk < blknum; blk++) {
		len = ntohl(blkarr[blk]);
		if (len <= RAB_POLYNOMIAL_MAX_BLOCK_SIZE) {
			ctx->blocks[blk].length = len;
			ctx->blocks[blk].offset = pos1;
			pos1 += len;
		} else {
			ctx->blocks[blk].length = 0;
			ctx->blocks[blk].index = len - RAB_POLYNOMIAL_MAX_BLOCK_SIZE - 1;
		}
	}
	for (blk = 0; blk < blknum; blk++) {
		if (ctx->blocks[blk].length > 0) {
			len = ctx->blocks[blk].length;
			pos1 = ctx->blocks[blk].offset;
		} else {
			oblk = ctx->blocks[blk].index;
			len = ctx->blocks[oblk].length;
			pos1 = ctx->blocks[oblk].offset;
		}
		memcpy(pos2, buf + pos1, len);
		pos2 += len;
		sz += len;
		if (sz > orig_size) {
			ctx->valid = 0;
			break;
		}
	}
	if (ctx->valid && sz < orig_size) {
		ctx->valid = 0;
	}
	*size = orig_size;
}

