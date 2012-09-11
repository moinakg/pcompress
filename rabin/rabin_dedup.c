/*
 * The rabin polynomial computation is derived from:
 * http://code.google.com/p/rabin-fingerprint-c/
 * 
 * originally created by Joel Lawrence Tucci on 09-March-2011.
 * 
 * Rabin polynomial portions Copyright (c) 2011 Joel Lawrence Tucci
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
#include <pthread.h>
#include <heapq.h>

#include "rabin_dedup.h"

#define	FORTY_PCNT(x) (((x)/5 << 1))

extern int lzma_init(void **data, int *level, ssize_t chunksize);
extern int lzma_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, uchar_t chdr, void *data);
extern int lzma_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, uchar_t chdr, void *data);
extern int lzma_deinit(void **data);
extern int bsdiff(u_char *old, bsize_t oldsize, u_char *new, bsize_t newsize,
       u_char *diff, u_char *scratch, bsize_t scratchsize);
extern bsize_t get_bsdiff_sz(u_char *pbuf);
extern int bspatch(u_char *pbuf, u_char *old, bsize_t oldsize, u_char *new,
	bsize_t *_newsize);

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
uint64_t ir[256];
static int inited = 0;

static uint32_t
rabin_min_blksz(uint64_t chunksize, int rab_blk_sz, const char *algo, int delta_flag)
{
	uint32_t min_blk;

	min_blk = 1 << (rab_blk_sz + RAB_BLK_MIN_BITS - 1);
	return (min_blk);
}

uint32_t
rabin_buf_extra(uint64_t chunksize, int rab_blk_sz, const char *algo, int delta_flag)
{
	if (rab_blk_sz < 1 || rab_blk_sz > 5)
		rab_blk_sz = RAB_BLK_DEFAULT;

	return ((chunksize / rabin_min_blksz(chunksize, rab_blk_sz, algo, delta_flag))
	    * sizeof (uint32_t));
}

/*
 * Initialize the algorithm with the default params.
 */
rabin_context_t *
create_rabin_context(uint64_t chunksize, uint64_t real_chunksize, int rab_blk_sz,
    const char *algo, int delta_flag) {
	rabin_context_t *ctx;
	unsigned char *current_window_data;
	uint32_t i;

	if (rab_blk_sz < 1 || rab_blk_sz > 5)
		rab_blk_sz = RAB_BLK_DEFAULT;

	/*
	 * Pre-compute a table of irreducible polynomial evaluations for each
	 * possible byte value.
	 */
	pthread_mutex_lock(&init_lock);
	if (!inited) {
		int term, j;
		uint64_t val;

		for (j = 0; j < 256; j++) {
			term = 1;
			val = 0;
			for (i=0; i<RAB_POLYNOMIAL_WIN_SIZE; i++) {
				if (term & FP_POLY) {
					val = (val << 1) + j;
				}
				term <<= 1;
			}
			ir[j] = val;
		}
		inited = 1;
	}
	pthread_mutex_unlock(&init_lock);

	/*
	 * Rabin window size must be power of 2 for optimization.
	 */
	if (!ISP2(RAB_POLYNOMIAL_WIN_SIZE)) {
		fprintf(stderr, "Rabin window size must be a power of 2 in range 4 <= x <= 64\n");
		return (NULL);
	}

	if (chunksize < RAB_MIN_CHUNK_SIZE) {
		fprintf(stderr, "Minimum chunk size for Dedup must be %llu bytes\n",
		    RAB_MIN_CHUNK_SIZE);
		return (NULL);
	}

	/*
	 * For LZMA with chunksize <= LZMA Window size and/or Delta enabled we
	 * use 4K minimum Rabin block size. For everything else it is 2K based
	 * on experimentation.
	 */
	ctx = (rabin_context_t *)slab_alloc(NULL, sizeof (rabin_context_t));
	ctx->rabin_poly_max_block_size = RAB_POLYNOMIAL_MAX_BLOCK_SIZE;

	ctx->rabin_break_patt = 0;
	ctx->delta_flag = delta_flag;
	ctx->rabin_poly_avg_block_size = 1 << (rab_blk_sz + RAB_BLK_MIN_BITS);
	ctx->rabin_avg_block_mask = ctx->rabin_poly_avg_block_size - 1;
	ctx->rabin_poly_min_block_size = rabin_min_blksz(chunksize, rab_blk_sz, algo, delta_flag);
	ctx->fp_mask = ctx->rabin_avg_block_mask | ctx->rabin_poly_avg_block_size;
	ctx->blknum = chunksize / ctx->rabin_poly_min_block_size;

	if (chunksize % ctx->rabin_poly_min_block_size)
		ctx->blknum++;

	if (ctx->blknum > RABIN_MAX_BLOCKS) {
		fprintf(stderr, "Chunk size too large for dedup.\n");
		destroy_rabin_context(ctx);
		return (NULL);
	}
	current_window_data = slab_alloc(NULL, RAB_POLYNOMIAL_WIN_SIZE);
	ctx->blocks = NULL;
	if (real_chunksize > 0) {
		ctx->blocks = (rabin_blockentry_t **)slab_calloc(NULL,
			ctx->blknum, sizeof (rabin_blockentry_t *));
	}
	if(ctx == NULL || current_window_data == NULL || (ctx->blocks == NULL && real_chunksize > 0)) {
		fprintf(stderr,
		    "Could not allocate rabin polynomial context, out of memory\n");
		destroy_rabin_context(ctx);
		return (NULL);
	}

	ctx->lzma_data = NULL;
	ctx->level = 14;
	if (real_chunksize > 0) {
		lzma_init(&(ctx->lzma_data), &(ctx->level), chunksize);
		if (!(ctx->lzma_data)) {
			fprintf(stderr,
			    "Could not initialize LZMA data for rabin index, out of memory\n");
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

	slab_cache_add(sizeof (rabin_blockentry_t));
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
}

void
destroy_rabin_context(rabin_context_t *ctx)
{
	if (ctx) {
		uint32_t i;
		if (ctx->current_window_data) slab_free(NULL, ctx->current_window_data);
		if (ctx->blocks) {
			for (i=0; i<ctx->blknum && ctx->blocks[i] != NULL; i++) {
				slab_free(NULL, ctx->blocks[i]);
			}
			slab_free(NULL, ctx->blocks);
		}
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
	rabin_blockentry_t *a1 = *((rabin_blockentry_t **)a);
	rabin_blockentry_t *b1 = *((rabin_blockentry_t **)b);

	if (a1->cksum_n_offset < b1->cksum_n_offset) {
		return (-1);
	} else if (a1->cksum_n_offset == b1->cksum_n_offset) {
		int l1 = a1->length;
		int l2 = b1->length;

		/*
		 * If fingerprints match then compare lengths. Length match makes
		 * for strong exact detection/ordering during sort while stopping
		 * short of expensive memcmp() during sorting.
		 * 
		 * Even though rabin_blockentry_t->length is unsigned we use signed
		 * int here to avoid branches. In practice a rabin block size at
		 * this point varies from 2K to 128K. The length is unsigned in
		 * order to support non-duplicate block merging and large blocks
		 * after this point.
		 */
		return (l1 - l2);
	} else {
		return (1);
	}
}

/**
 * Perform Deduplication based on Rabin Fingerprinting. A 31-byte window is used for
 * the rolling checksum and dedup blocks vary in size from 4K-128K.
 */
uint32_t
rabin_dedup(rabin_context_t *ctx, uchar_t *buf, ssize_t *size, ssize_t offset, ssize_t *rabin_pos)
{
	ssize_t i, last_offset, j, fplist_sz;
	uint32_t blknum;
	char *buf1 = (char *)buf;
	uint32_t length;
	uint64_t cur_roll_checksum, cur_pos_checksum, cur_sketch;
	uint32_t *fplist;
	heap_t heap;

	if (rabin_pos == NULL) {
		/*
		 * Initialize arrays for sketch computation. We re-use memory allocated
		 * for the compressed chunk temporarily.
		 */
		fplist_sz = 4 * ctx->rabin_poly_max_block_size;
		fplist = (uint32_t *)(ctx->cbuf + ctx->real_chunksize - fplist_sz);
		memset(fplist, 0, fplist_sz);
		reset_heap(&heap, fplist_sz/2);
	}
	length = offset;
	last_offset = 0;
	blknum = 0;
	ctx->valid = 0;
	cur_roll_checksum = 0;
	cur_sketch = 0;

	/* 
	 * If rabin_pos is non-zero then we are being asked to scan for the last rabin boundary
	 * in the chunk. We start scanning at chunk end - max rabin block size. We avoid doing
	 * a full chunk scan.
	 * 
	 * !!!NOTE!!!: Code duplication below for performance.
	 */
	if (rabin_pos) {
		offset = *size - ctx->rabin_poly_max_block_size;
		length = 0;
		for (i=offset; i<*size; i++) {
			uchar_t cur_byte = buf1[i];
			uint64_t pushed_out = ctx->current_window_data[ctx->window_pos];

			ctx->current_window_data[ctx->window_pos] = cur_byte;
			cur_roll_checksum = (cur_roll_checksum << 1) + cur_byte;
			cur_roll_checksum -= (pushed_out << RAB_POLYNOMIAL_WIN_SIZE);
			cur_pos_checksum = cur_roll_checksum ^ ir[pushed_out];

			ctx->window_pos = (ctx->window_pos + 1) & (RAB_POLYNOMIAL_WIN_SIZE-1);
			length++;
			if (length < ctx->rabin_poly_min_block_size) continue;

			// If we hit our special value update block offset
			if ((cur_pos_checksum & ctx->rabin_avg_block_mask) == ctx->rabin_break_patt) {
				last_offset = i;
				length = 0;
			}
		}

		if (last_offset < *size) {
			*rabin_pos = last_offset;
		}
		return (0);
	}

	if (*size < ctx->rabin_poly_avg_block_size) return;
	j = 0;

	for (i=offset; i<*size; i++) {
		uint32_t *splits;
		uchar_t cur_byte = buf1[i];
		uint64_t pushed_out = ctx->current_window_data[ctx->window_pos];
		ctx->current_window_data[ctx->window_pos] = cur_byte;
		/*
		 * We want to do:
		 * cur_roll_checksum = cur_roll_checksum * RAB_POLYNOMIAL_CONST + cur_byte;
		 * cur_roll_checksum -= pushed_out * polynomial_pow;
		 *
		 * However since RAB_POLYNOMIAL_CONST == 2, we use shifts.
		 */
		cur_roll_checksum = (cur_roll_checksum << 1) + cur_byte;
		cur_roll_checksum -= (pushed_out << RAB_POLYNOMIAL_WIN_SIZE);
		cur_pos_checksum = cur_roll_checksum ^ ir[pushed_out];

		/*
		 * Retain a list of all fingerprints in the block. We then compute
		 * the K min values sketch from that list and generate a super sketch
		 * by hashing over the K min values sketch. We only store the least
		 * significant 32 bits of the fingerprint. This uses less memory,
		 * requires smaller memset() calls and generates a sufficiently large
		 * number of similarity matches without false positives - determined
		 * by experimentation.
		 * 
		 * This is called minhashing and is used widely, for example in various
		 * search engines to detect similar documents.
		 */
		fplist[j] = cur_pos_checksum & 0xFFFFFFFFUL;
		j++;

		/*
		 * Window pos has to rotate from 0 .. RAB_POLYNOMIAL_WIN_SIZE-1
		 * We avoid a branch here by masking. This requires RAB_POLYNOMIAL_WIN_SIZE
		 * to be power of 2
		 */
		ctx->window_pos = (ctx->window_pos + 1) & (RAB_POLYNOMIAL_WIN_SIZE-1);
		length++;
		if (length < ctx->rabin_poly_min_block_size) continue;

		// If we hit our special value or reached the max block size update block offset
		if ((cur_pos_checksum & ctx->rabin_avg_block_mask) == ctx->rabin_break_patt ||
		    length >= ctx->rabin_poly_max_block_size) {
			if (ctx->blocks[blknum] == 0)
				ctx->blocks[blknum] = (rabin_blockentry_t *)slab_alloc(NULL, sizeof (rabin_blockentry_t));
			ctx->blocks[blknum]->offset = last_offset;
			ctx->blocks[blknum]->index = blknum; // Need to store for sorting
			ctx->blocks[blknum]->length = length;
			ctx->blocks[blknum]->ref = 0;
			ctx->blocks[blknum]->similar = 0;
			ctx->blocks[blknum]->crc = XXH_strong32(buf1+last_offset, length, 0);

			if (ctx->delta_flag) {
				/*
				 * Reset the heap structure and find the K min values. We use a
				 * min heap mechanism taken from the heap based priority queue
				 * implementation in Python.
				 * Here K = 40%. We are aiming to detect 40% similarity on average.
				 */
				reset_heap(&heap, FORTY_PCNT(j));
				ksmallest(fplist, j, &heap);
				cur_sketch = XXH_fast32((const uchar_t *)fplist,  FORTY_PCNT(j)*4, 0);
				memset(fplist, 0, fplist_sz);
			} else {
				cur_sketch = ctx->blocks[blknum]->crc;
			}
			ctx->blocks[blknum]->cksum_n_offset = cur_sketch;
			cur_sketch = 0;
			blknum++;
			last_offset = i+1;
			length = 0;
			j = 0;
		}
	}

	DEBUG_STAT_EN(printf("Original size: %lld, blknum: %u\n", *size, blknum));
	// If we found at least a few chunks, perform dedup.
	if (blknum > 2) {
		uint32_t blk, prev_index, prev_length;
		ssize_t pos, matchlen, pos1;
		int valid = 1;
		char *tmp;
		uint32_t *blkarr, *trans, *rabin_index;
		ssize_t rabin_index_sz;
		rabin_blockentry_t *prev;
		DEBUG_STAT_EN(uint32_t delta_calls, delta_fails);
		DEBUG_STAT_EN(delta_calls = 0);
		DEBUG_STAT_EN(delta_fails = 0);

		// Insert the last left-over trailing bytes, if any, into a block.
		if (last_offset < *size) {
			if (ctx->blocks[blknum] == 0)
				ctx->blocks[blknum] = (rabin_blockentry_t *)slab_alloc(NULL,
					sizeof (rabin_blockentry_t));
			ctx->blocks[blknum]->offset = last_offset;
			ctx->blocks[blknum]->index = blknum;
			ctx->blocks[blknum]->length = *size - last_offset;
			ctx->blocks[blknum]->ref = 0;
			ctx->blocks[blknum]->similar = 0;
			ctx->blocks[blknum]->crc = XXH_strong32(buf1+last_offset, ctx->blocks[blknum]->length, 0);

			if (ctx->delta_flag) {
				j = (j > 0 ? j:1);
				if (j > 1) {
					reset_heap(&heap, FORTY_PCNT(j));
					ksmallest(fplist, j, &heap);
					cur_sketch =
					    XXH_fast32((const uchar_t *)fplist,  FORTY_PCNT(j)*4, 0);
				} else {
					cur_sketch =
					    XXH_fast32((const uchar_t *)fplist, (j*4)/2, 0);
				}
			} else {
				cur_sketch = ctx->blocks[blknum]->crc;
			}

			ctx->blocks[blknum]->cksum_n_offset = cur_sketch;
			blknum++;
			last_offset = *size;
		}

		rabin_index_sz = (ssize_t)blknum * RABIN_ENTRY_SIZE;

		/*
		 * Now sort the block array based on checksums. This will bring virtually 
		 * all similar block entries together. Effectiveness depends on how strong
		 * our checksum is. We are using a maximal super-sketch value.
		 */
		qsort(ctx->blocks, blknum, sizeof (rabin_blockentry_t *), cmpblks);
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
			blkarr[ctx->blocks[blk]->index] = blk;

			if (blk > 0 && ctx->blocks[blk]->cksum_n_offset == prev->cksum_n_offset &&
			    ctx->blocks[blk]->length == prev->length &&
			    ctx->blocks[blk]->crc == prev->crc &&
			    memcmp(buf1 + prev->offset, buf1 + ctx->blocks[blk]->offset,
				prev->length) == 0)
			{
				ctx->blocks[blk]->similar = SIMILAR_EXACT;
				ctx->blocks[blk]->index = prev->index;
				prev->ref = 1;
				matchlen += prev->length;
				continue;
			}
			prev = ctx->blocks[blk];
		}

		prev = NULL;
		if (ctx->delta_flag) {
			for (blk = 0; blk < blknum; blk++) {
				if (ctx->blocks[blk]->similar) continue;

				/*
				 * Compare blocks for similarity.
				 * Note: Block list by now is sorted by length as well.
				 */
				if (prev != NULL && ctx->blocks[blk]->ref == 0 &&
				    ctx->blocks[blk]->cksum_n_offset == prev->cksum_n_offset &&
				    ctx->blocks[blk]->length - prev->length < 512 &&
				    ctx->blocks[blk]->mean_n_length == prev->mean_n_length
				) {
					ctx->blocks[blk]->index = prev->index;
					ctx->blocks[blk]->similar = SIMILAR_PARTIAL;
					prev->ref = 1;
					matchlen += (prev->length>>1);
					continue;
				}
				prev = ctx->blocks[blk];
			}
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

			be = ctx->blocks[blkarr[blk]];
			if (be->similar == 0) {
				/*
				 * Update Index entry with the length. Also try to merge runs
				 * of unique (non-duplicate/similar) blocks into a single block
				 * entry as long as the total length does not exceed max block
				 * size.
				 */
				if (prev_index == 0) {
					if (be->ref == 0) {
						prev_index = pos;
						prev_length = be->length;
					}
					rabin_index[pos] = be->length;
					ctx->blocks[pos]->cksum_n_offset = be->offset;
					trans[blk] = pos;
					pos++;
				} else {
					if (be->ref > 0) {
						prev_index = 0;
						prev_length = 0;
						rabin_index[pos] = be->length;
						ctx->blocks[pos]->cksum_n_offset = be->offset;
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
							ctx->blocks[pos]->cksum_n_offset = be->offset;
							trans[blk] = pos;
							pos++;
						}
					}
				}
			} else {
				prev_index = 0;
				prev_length = 0;
				ctx->blocks[pos]->cksum_n_offset = be->offset;
				ctx->blocks[pos]->mean_n_length = be->length;
				trans[blk] = pos;

				if (be->similar == SIMILAR_EXACT) {
					rabin_index[pos] = (blkarr[be->index] | RABIN_INDEX_FLAG) &
					    CLEAR_SIMILARITY_FLAG;
				} else {
					rabin_index[pos] = blkarr[be->index] | RABIN_INDEX_FLAG |
					    SET_SIMILARITY_FLAG;
				}
				pos++;
			}
		}

		/*
		 * Final pass, copy the data and perform delta encoding.
		 */
		blknum = pos;
		rabin_index_sz = (ssize_t)pos * RABIN_ENTRY_SIZE;
		pos1 = rabin_index_sz + RABIN_HDR_SIZE;
		for (blk = 0; blk < blknum; blk++) {
			uchar_t *old, *new;
			int32_t bsz;

			/*
			 * If blocks are overflowing the allowed chunk size then dedup did not
			 * help at all. We invalidate the dedup operation.
			 */
			if (pos1 > last_offset) {
				valid = 0;
				break;
			}
			if (rabin_index[blk] & RABIN_INDEX_FLAG) {
				j = rabin_index[blk] & RABIN_INDEX_VALUE;
				i = ctx->blocks[j]->index;

				if (rabin_index[blk] & GET_SIMILARITY_FLAG) {
					old = buf1 + ctx->blocks[j]->offset;
					new = buf1 + ctx->blocks[blk]->cksum_n_offset;
					matchlen = ctx->real_chunksize - *size;
					DEBUG_STAT_EN(delta_calls++);

					bsz = bsdiff(old, ctx->blocks[j]->length, new,
					    ctx->blocks[blk]->mean_n_length, ctx->cbuf + pos1,
					    buf1 + *size, matchlen);
					if (bsz == 0) {
						DEBUG_STAT_EN(delta_fails++);
						memcpy(ctx->cbuf + pos1, new, ctx->blocks[blk]->mean_n_length);
						rabin_index[blk] = htonl(ctx->blocks[blk]->mean_n_length);
						pos1 += ctx->blocks[blk]->mean_n_length;
					} else {
						rabin_index[blk] = htonl(trans[i] |
						    RABIN_INDEX_FLAG | SET_SIMILARITY_FLAG);
						pos1 += bsz;
					}
				} else {
					rabin_index[blk] = htonl(trans[i] | RABIN_INDEX_FLAG);
				}
			} else {
				memcpy(ctx->cbuf + pos1, buf1 + ctx->blocks[blk]->cksum_n_offset,
				    rabin_index[blk]);
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
			DEBUG_STAT_EN(printf("Deduped size: %lld, blknum: %u, delta_calls: %u, delta_fails: %u\n",
					     *size, blknum, delta_calls, delta_fails));
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

	slab_cache_add(sizeof (rabin_blockentry_t));
	for (blk = 0; blk < blknum; blk++) {
		if (ctx->blocks[blk] == 0)
			ctx->blocks[blk] = (rabin_blockentry_t *)slab_alloc(NULL, sizeof (rabin_blockentry_t));
		len = ntohl(rabin_index[blk]);
		if (len == 0) {
			ctx->blocks[blk]->length = 0;
			ctx->blocks[blk]->index = 0;

		} else if (!(len & RABIN_INDEX_FLAG)) {
			ctx->blocks[blk]->length = len;
			ctx->blocks[blk]->offset = pos1;
			pos1 += len;
		} else {
			bsize_t blen;

			ctx->blocks[blk]->length = 0;
			if (len & GET_SIMILARITY_FLAG) {
				ctx->blocks[blk]->offset = pos1;
				ctx->blocks[blk]->index = (len & RABIN_INDEX_VALUE) | SET_SIMILARITY_FLAG;
				blen = get_bsdiff_sz(buf + pos1);
				pos1 += blen;
			} else {
				ctx->blocks[blk]->index = len & RABIN_INDEX_VALUE;
			}
		}
	}

	for (blk = 0; blk < blknum; blk++) {
		int rv;
		bsize_t newsz;

		if (ctx->blocks[blk]->length == 0 && ctx->blocks[blk]->index == 0) continue;
		if (ctx->blocks[blk]->length > 0) {
			len = ctx->blocks[blk]->length;
			pos1 = ctx->blocks[blk]->offset;
		} else {
			oblk = ctx->blocks[blk]->index;

			if (oblk & GET_SIMILARITY_FLAG) {
				oblk = oblk & CLEAR_SIMILARITY_FLAG;
				len = ctx->blocks[oblk]->length;
				pos1 = ctx->blocks[oblk]->offset;
				newsz = data_sz - sz;
				rv = bspatch(buf + ctx->blocks[blk]->offset, buf + pos1, len, pos2, &newsz);
				if (rv == 0) {
					fprintf(stderr, "Failed to bspatch block.\n");
					ctx->valid = 0;
					break;
				}
				pos2 += newsz;
				sz += newsz;
				if (sz > data_sz) {
					fprintf(stderr, "Dedup data overflows chunk.\n");
					ctx->valid = 0;
					break;
				}
				continue;
			} else {
				len = ctx->blocks[oblk]->length;
				pos1 = ctx->blocks[oblk]->offset;
			}
		}
		memcpy(pos2, buf + pos1, len);
		pos2 += len;
		sz += len;
		if (sz > data_sz) {
			fprintf(stderr, "Dedup data overflows chunk.\n");
			ctx->valid = 0;
			break;
		}
	}
	if (ctx->valid && sz < data_sz) {
		fprintf(stderr, "Too little dedup data processed.\n");
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
