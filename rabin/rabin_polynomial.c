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
 * version 3 of the License, or (at your option) any later version.
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

extern int lzma_init(void **data, int *level, ssize_t chunksize);
extern int lzma_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, uchar_t chdr, void *data);
extern int lzma_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data);
extern int lzma_deinit(void **data);

uint32_t rabin_polynomial_max_block_size = RAB_POLYNOMIAL_MAX_BLOCK_SIZE;

/*
 * Initialize the algorithm with the default params.
 */
rabin_context_t *
create_rabin_context(uint64_t chunksize, uint64_t real_chunksize, const char *algo) {
	rabin_context_t *ctx;
	unsigned char *current_window_data;
	uint32_t blknum;
	int level = 14;

	/*
	 * For LZMA with chunksize <= LZMA Window size we use 4K minimum Rabin
	 * block size. For everything else it is 1K based on experimentation.
	 */
	ctx = (rabin_context_t *)slab_alloc(NULL, sizeof (rabin_context_t));
	ctx->rabin_poly_max_block_size = RAB_POLYNOMIAL_MAX_BLOCK_SIZE;
	if (memcmp(algo, "lzma", 4) == 0 && chunksize <= LZMA_WINDOW_MAX) {
		ctx->rabin_poly_min_block_size = RAB_POLYNOMIAL_MIN_BLOCK_SIZE;
		ctx->rabin_avg_block_mask = RAB_POLYNOMIAL_AVG_BLOCK_MASK;
		ctx->rabin_poly_avg_block_size = RAB_POLYNOMIAL_AVG_BLOCK_SIZE;
		ctx->rabin_break_patt = RAB_POLYNOMIAL_CONST;
	} else {
		ctx->rabin_poly_min_block_size = RAB_POLYNOMIAL_MIN_BLOCK_SIZE2;
		ctx->rabin_avg_block_mask = RAB_POLYNOMIAL_AVG_BLOCK_MASK2;
		ctx->rabin_poly_avg_block_size = RAB_POLYNOMIAL_AVG_BLOCK_SIZE2;
		ctx->rabin_break_patt = 0;
	}

	blknum = chunksize / ctx->rabin_poly_min_block_size;
	if (chunksize % ctx->rabin_poly_min_block_size)
		blknum++;

	if (blknum > RABIN_MAX_BLOCKS) {
		fprintf(stderr, "Chunk size too large for dedup.\n");
		destroy_rabin_context(ctx);
		return (NULL);
	}
	current_window_data = slab_alloc(NULL, RAB_POLYNOMIAL_WIN_SIZE);
	ctx->blocks = NULL;
	if (real_chunksize > 0) {
		ctx->blocks = (rabin_blockentry_t *)slab_alloc(NULL,
			blknum * ctx->rabin_poly_min_block_size);
	}
	if(ctx == NULL || current_window_data == NULL || (ctx->blocks == NULL && real_chunksize > 0)) {
		fprintf(stderr,
		    "Could not allocate rabin polynomial context, out of memory\n");
		destroy_rabin_context(ctx);
		return (NULL);
	}

	ctx->lzma_data = NULL;
	if (real_chunksize > 0) {
		lzma_init(&(ctx->lzma_data), &(ctx->level), chunksize);
		if (!(ctx->lzma_data)) {
			fprintf(stderr,
			    "Could not allocate rabin polynomial context, out of memory\n");
			destroy_rabin_context(ctx);
			return (NULL);
		}
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
	ctx->real_chunksize = real_chunksize;
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
	if (ctx) {
		if (ctx->current_window_data) slab_free(NULL, ctx->current_window_data);
		if (ctx->blocks) slab_free(NULL, ctx->blocks);
		if (ctx->lzma_data) lzma_deinit(&(ctx->lzma_data));
		slab_free(NULL, ctx);
	}
}

/*
 * Checksum Comparator for qsort
 */
static int
cmpblks(const void *a, const void *b)
{
	rabin_blockentry_t *a1 = (rabin_blockentry_t *)a;
	rabin_blockentry_t *b1 = (rabin_blockentry_t *)b;

	if (a1->cksum_n_offset < b1->cksum_n_offset)
		return (-1);
	else if (a1->cksum_n_offset == b1->cksum_n_offset)
		return (0);
	else if (a1->cksum_n_offset > b1->cksum_n_offset)
		return (1);
}

/**
 * Perform Deduplication based on Rabin Fingerprinting. A 31-byte window is used for
 * the rolling checksum and dedup blocks vary in size from 4K-128K.
 */
uint32_t
rabin_dedup(rabin_context_t *ctx, uchar_t *buf, ssize_t *size, ssize_t offset, ssize_t *rabin_pos)
{
	ssize_t i, last_offset,j;
	uint32_t blknum;
	char *buf1 = (char *)buf;
	uint32_t length;

	length = offset;
	last_offset = 0;
	blknum = 0;
	ctx->valid = 0;
	ctx->cur_checksum = 0;

	/* 
	 * If rabin_pos is non-zero then we are being asked to scan for the last rabin boundary
	 * in the chunk. We start scanning at chunk end - max rabin block size. We avoid doing
	 * a full chunk scan.
	 */
	if (rabin_pos) {
		offset = *size - RAB_POLYNOMIAL_MAX_BLOCK_SIZE;
	}
	if (*size < ctx->rabin_poly_avg_block_size) return;
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

		if (length < ctx->rabin_poly_min_block_size) continue;

		// If we hit our special value or reached the max block size update block offset
		if ((ctx->cur_roll_checksum & ctx->rabin_avg_block_mask) == ctx->rabin_break_patt ||
		    length >= rabin_polynomial_max_block_size) {
			if (rabin_pos == NULL) {
				ctx->blocks[blknum].offset = last_offset;
				ctx->blocks[blknum].index = blknum; // Need to store for sorting
				ctx->blocks[blknum].cksum_n_offset = ctx->cur_checksum;
				ctx->blocks[blknum].length = length;
				ctx->blocks[blknum].refcount = 0;
				blknum++;
			}
			ctx->cur_checksum = 0;
			last_offset = i+1;
			length = 0;
		}
	}

	if (rabin_pos && last_offset < *size) {
		*rabin_pos = last_offset;
		return (0);
	}
	// If we found at least a few chunks, perform dedup.
	if (blknum > 2) {
		uint64_t prev_cksum;
		uint32_t blk, prev_length;
		ssize_t pos, matchlen, pos1;
		int valid = 1;
		char *tmp, *prev_offset;
		uint32_t *blkarr, *trans, *rabin_index, prev_index, prev_blk;
		ssize_t rabin_index_sz;

		// Insert the last left-over trailing bytes, if any, into a block.
		if (last_offset < *size) {
			ctx->blocks[blknum].offset = last_offset;
			ctx->blocks[blknum].index = blknum;
			ctx->blocks[blknum].cksum_n_offset = ctx->cur_checksum;
			ctx->blocks[blknum].length = *size - last_offset;
			ctx->blocks[blknum].refcount = 0;
			blknum++;
			ctx->cur_checksum = 0;
			last_offset = *size;
		}

		rabin_index_sz = (ssize_t)blknum * RABIN_ENTRY_SIZE;
		prev_cksum = 0;
		prev_length = 0;
		prev_offset = 0;

		/*
		 * Now sort the block array based on checksums. This will bring virtually 
		 * all similar block entries together. Effectiveness depends on how strong
		 * our checksum is. We are using CRC64 here so we should be pretty okay.
		 * TODO: Test with a heavily optimized MD5 (from OpenSSL?) later.
		 */
		qsort(ctx->blocks, blknum, sizeof (rabin_blockentry_t), cmpblks);
		rabin_index = (uint32_t *)(ctx->cbuf + RABIN_HDR_SIZE);

		/*
		 * We need 2 temporary arrays. We just use available space in the last
		 * portion of the buffer that will hold the deduped segment.
		 */
		blkarr = (uint32_t *)(ctx->cbuf + ctx->real_chunksize - (rabin_index_sz * 2 + 1));
		trans = (uint32_t *)(ctx->cbuf + ctx->real_chunksize - (rabin_index_sz + 1));
		matchlen = 0;

		/*
		 * Now make a pass through the sorted block array making identical blocks
		 * point to the first identical block entry. A simple Run Length Encoding
		 * sort of. Checksums, length and contents (memcmp()) must match for blocks
		 * to be considered identical.
		 * The block index in the chunk is initialized with pointers into the
		 * sorted block array.
		 * A reference count is maintained for blocks that are similar with other
		 * blocks. This helps in non-duplicate block merging later.
		 */
		for (blk = 0; blk < blknum; blk++) {
			blkarr[ctx->blocks[blk].index] = blk;

			if (blk > 0 && ctx->blocks[blk].cksum_n_offset == prev_cksum &&
			    ctx->blocks[blk].length == prev_length &&
			    memcmp(prev_offset, buf1 + ctx->blocks[blk].offset, prev_length) == 0) {
				ctx->blocks[blk].length = 0;
				ctx->blocks[blk].index = prev_index;
				(ctx->blocks[prev_blk].refcount)++;
				matchlen += prev_length;
				continue;
			}

			prev_offset = buf1 + ctx->blocks[blk].offset;
			prev_cksum = ctx->blocks[blk].cksum_n_offset;
			prev_length = ctx->blocks[blk].length;
			prev_index = ctx->blocks[blk].index;
			prev_blk = blk;
		}

		if (matchlen < rabin_index_sz) {
			ctx->valid = 0;
			return;
		}
		/*
		 * Another pass, this time through the block index in the chunk. We insert
		 * block length into unique block entries. For block entries that are
		 * identical with another one we store the index number with msb set.
		 * This way we can differentiate between a unique block length entry and a
		 * pointer to another block without needing a separate flag.
		 */
		prev_index = 0;
		prev_length = 0;
		pos = 0;
		for (blk = 0; blk < blknum; blk++) {
			rabin_blockentry_t *be;

			be = &(ctx->blocks[blkarr[blk]]);
			if (be->length > 0) {
				/*
				 * Update Index entry with the length. Also try to merge runs
				 * of unique (non-duplicate) blocks into a single block entry
				 * as long as the total length does not exceed max block size.
				 */
				if (prev_index == 0) {
					if (be->refcount == 0) {
						prev_index = pos;
						prev_length = be->length;
					}
					rabin_index[pos] = be->length;
					ctx->blocks[pos].cksum_n_offset = be->offset;
					trans[blk] = pos;
					pos++;
				} else {
					if (be->refcount > 0) {
						prev_index = 0;
						prev_length = 0;
						rabin_index[pos] = be->length;
						ctx->blocks[pos].cksum_n_offset = be->offset;
						trans[blk] = pos;
						pos++;
					} else {
						if (prev_length + be->length <= RABIN_MAX_BLOCK_SIZE) {
							prev_length += be->length;
							rabin_index[prev_index] = prev_length;
						} else {
							prev_index = 0;
							prev_length = 0;
							rabin_index[pos] = be->length;
							ctx->blocks[pos].cksum_n_offset = be->offset;
							trans[blk] = pos;
							pos++;
						}
					}
				}
			} else {
				prev_index = 0;
				prev_length = 0;
				rabin_index[pos] = be->index | RABIN_INDEX_FLAG;
				trans[blk] = pos;
				pos++;
			}
		}

		/*
		 * Final pass, copy the data.
		 */
		blknum = pos;
		rabin_index_sz = (ssize_t)pos * RABIN_ENTRY_SIZE;
		pos1 = rabin_index_sz + RABIN_HDR_SIZE;
		for (blk = 0; blk < blknum; blk++) {
			if (rabin_index[blk] & RABIN_INDEX_FLAG) {
				j = rabin_index[blk] & RABIN_INDEX_VALUE;
				rabin_index[blk] = htonl(trans[j] | RABIN_INDEX_FLAG);
			} else {
				/*
				 * If blocks are overflowing the allowed chunk size then dedup did not
				 * help at all. We invalidate the dedup operation.
				 */
				if (pos1 > last_offset) {
					valid = 0;
					break;
				}
				memcpy(ctx->cbuf + pos1, buf1 + ctx->blocks[blk].cksum_n_offset, rabin_index[blk]);
				pos1 += rabin_index[blk];
				rabin_index[blk] = htonl(rabin_index[blk]);
			}
		}
cont:
		if (valid) {
			uchar_t *cbuf = ctx->cbuf;
			ssize_t *entries;

			*((uint32_t *)cbuf) = htonl(blknum);
			cbuf += sizeof (uint32_t);
			entries = (ssize_t *)cbuf;
			entries[0] = htonll(*size);
			entries[1] = 0;
			entries[2] = htonll(pos1 - rabin_index_sz - RABIN_HDR_SIZE);
			*size = pos1;
			ctx->valid = 1;

			/*
			 * Remaining header entries: size of compressed index and size of
			 * compressed data are inserted later via rabin_update_hdr, after actual compression!
			 */
			return (rabin_index_sz);
		}
	}
	return (0);
}

void
rabin_update_hdr(uchar_t *buf, ssize_t rabin_index_sz_cmp, ssize_t rabin_data_sz_cmp)
{
	ssize_t *entries;

	buf += sizeof (uint32_t);
	entries = (ssize_t *)buf;
	entries[1] = htonll(rabin_index_sz_cmp);
	entries[3] = htonll(rabin_data_sz_cmp);
}

void
rabin_parse_hdr(uchar_t *buf, uint32_t *blknum, ssize_t *rabin_index_sz,
		ssize_t *rabin_data_sz, ssize_t *rabin_index_sz_cmp,
		ssize_t *rabin_data_sz_cmp, ssize_t *rabin_deduped_size)
{
	ssize_t *entries;

	*blknum = ntohl(*((uint32_t *)(buf)));
	buf += sizeof (uint32_t);

	entries = (ssize_t *)buf;
	*rabin_data_sz = ntohll(entries[0]);
	*rabin_index_sz = (ssize_t)(*blknum) * RABIN_ENTRY_SIZE;
	*rabin_index_sz_cmp =  ntohll(entries[1]);
	*rabin_deduped_size = ntohll(entries[2]);
	*rabin_data_sz_cmp = ntohll(entries[3]);
}

void
rabin_inverse_dedup(rabin_context_t *ctx, uchar_t *buf, ssize_t *size)
{
	uint32_t blknum, blk, oblk, len;
	uint32_t *rabin_index;
	ssize_t data_sz, sz, indx_cmp, data_sz_cmp, deduped_sz;
	ssize_t rabin_index_sz, pos1, i;
	uchar_t *pos2;

	rabin_parse_hdr(buf, &blknum, &rabin_index_sz, &data_sz, &indx_cmp, &data_sz_cmp, &deduped_sz);
	rabin_index = (uint32_t *)(buf + RABIN_HDR_SIZE);
	pos1 = rabin_index_sz + RABIN_HDR_SIZE;
	pos2 = ctx->cbuf;
	sz = 0;
	ctx->valid = 1;

	for (blk = 0; blk < blknum; blk++) {
		len = ntohl(rabin_index[blk]);
		if (len == 0) {
			ctx->blocks[blk].length = 0;
			ctx->blocks[blk].index = 0;

		} else if (!(len & RABIN_INDEX_FLAG)) {
			ctx->blocks[blk].length = len;
			ctx->blocks[blk].offset = pos1;
			pos1 += len;
		} else {
			ctx->blocks[blk].length = 0;
			ctx->blocks[blk].index = len & RABIN_INDEX_VALUE;
		}
	}
	for (blk = 0; blk < blknum; blk++) {
		if (ctx->blocks[blk].length == 0 && ctx->blocks[blk].index == 0) continue;
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
		if (sz > data_sz) {
			ctx->valid = 0;
			break;
		}
	}
	if (ctx->valid && sz < data_sz) {
		ctx->valid = 0;
	}
	*size = data_sz;
}

/*
 * TODO: Consolidate rabin dedup and compression/decompression in functions here rather than
 * messy code in main program.
int
rabin_compress(rabin_context_t *ctx, uchar_t *from, ssize_t fromlen, uchar_t *to, ssize_t *tolen,
    int level, char chdr, void *data, compress_func_ptr cmp)
{
}
*/
