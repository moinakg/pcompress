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

#define	FORTY_PCNT(x) ((x)/5 << 1)
#define	FIFTY_PCNT(x) ((x) >> 1)
#define	SIXTY_PCNT(x) (((x) >> 1) + ((x) >> 3))

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
dedupe_min_blksz(int rab_blk_sz)
{
	uint32_t min_blk;

	min_blk = 1 << (rab_blk_sz + RAB_BLK_MIN_BITS - 1);
	return (min_blk);
}

uint32_t
dedupe_buf_extra(uint64_t chunksize, int rab_blk_sz, const char *algo, int delta_flag)
{
	if (rab_blk_sz < 1 || rab_blk_sz > 5)
		rab_blk_sz = RAB_BLK_DEFAULT;

	return ((chunksize / dedupe_min_blksz(rab_blk_sz)) * sizeof (uint32_t));
}

/*
 * Initialize the algorithm with the default params.
 */
dedupe_context_t *
create_dedupe_context(uint64_t chunksize, uint64_t real_chunksize, int rab_blk_sz,
    const char *algo, int delta_flag, int fixed_flag) {
	dedupe_context_t *ctx;
	unsigned char *current_window_data;
	uint32_t i;

	if (rab_blk_sz < 1 || rab_blk_sz > 5)
		rab_blk_sz = RAB_BLK_DEFAULT;

	if (fixed_flag) {
		delta_flag = 0;
		inited = 1;
	}

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
					val += term * j;
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
	ctx = (dedupe_context_t *)slab_alloc(NULL, sizeof (dedupe_context_t));
	ctx->rabin_poly_max_block_size = RAB_POLYNOMIAL_MAX_BLOCK_SIZE;

	ctx->fixed_flag = fixed_flag;
	ctx->rabin_break_patt = 0;
	ctx->rabin_poly_avg_block_size = 1 << (rab_blk_sz + RAB_BLK_MIN_BITS);
	ctx->rabin_avg_block_mask = ctx->rabin_poly_avg_block_size - 1;
	ctx->rabin_poly_min_block_size = dedupe_min_blksz(rab_blk_sz);
	ctx->fp_mask = ctx->rabin_avg_block_mask | ctx->rabin_poly_avg_block_size;
	ctx->delta_flag = 0;

	/*
	 * Scale down similarity percentage based on avg block size unless user specified
	 * argument '-EE' in which case fixed 40% match is used for Delta compression.
	 */
	if (delta_flag == DELTA_NORMAL) {
		if (ctx->rabin_poly_avg_block_size < (1 << 14)) {
			ctx->delta_flag = 1;
		} else if (ctx->rabin_poly_avg_block_size < (1 << 16)) {
			ctx->delta_flag = 2;
		} else {
			ctx->delta_flag = 3;
		}
	} else if (delta_flag == DELTA_EXTRA) {
		ctx->delta_flag = 1;
	}

	if (!fixed_flag)
		ctx->blknum = chunksize / ctx->rabin_poly_min_block_size;
	else
		ctx->blknum = chunksize / ctx->rabin_poly_avg_block_size;

	if (chunksize % ctx->rabin_poly_min_block_size)
		ctx->blknum++;

	if (ctx->blknum > RABIN_MAX_BLOCKS) {
		fprintf(stderr, "Chunk size too large for dedup.\n");
		destroy_dedupe_context(ctx);
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
		destroy_dedupe_context(ctx);
		return (NULL);
	}

	ctx->lzma_data = NULL;
	ctx->level = 14;
	if (real_chunksize > 0) {
		lzma_init(&(ctx->lzma_data), &(ctx->level), chunksize);
		if (!(ctx->lzma_data)) {
			fprintf(stderr,
			    "Could not initialize LZMA data for dedupe index, out of memory\n");
			destroy_dedupe_context(ctx);
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
	reset_dedupe_context(ctx);
	return (ctx);
}

void
reset_dedupe_context(dedupe_context_t *ctx)
{
	memset(ctx->current_window_data, 0, RAB_POLYNOMIAL_WIN_SIZE);
	ctx->window_pos = 0;
}

void
destroy_dedupe_context(dedupe_context_t *ctx)
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

/**
 * Perform Deduplication.
 * Both Semi-Rabin fingerprinting based and Fixed Block Deduplication are supported.
 * A 16-byte window is used for the rolling checksum and dedup blocks can vary in size
 * from 4K-128K.
 */
uint32_t
dedupe_compress(dedupe_context_t *ctx, uchar_t *buf, ssize_t *size, ssize_t offset, ssize_t *rabin_pos)
{
	ssize_t i, last_offset, j, ary_sz;
	uint32_t blknum;
	char *buf1 = (char *)buf;
	uint32_t length;
	uint64_t cur_roll_checksum, cur_pos_checksum;
	uint32_t *fplist;
	rabin_blockentry_t **htab;
	heap_t heap;

	length = offset;
	last_offset = 0;
	blknum = 0;
	ctx->valid = 0;
	cur_roll_checksum = 0;

	if (ctx->fixed_flag) {
		blknum = *size / ctx->rabin_poly_avg_block_size;
		j = *size % ctx->rabin_poly_avg_block_size;
		if (j) blknum++;

		last_offset = 0;
		length = ctx->rabin_poly_avg_block_size;
		for (i=0; i<blknum; i++) {
			if (i == blknum-1) {
				length = j;
			}
			if (ctx->blocks[i] == 0) {
				ctx->blocks[i] = (rabin_blockentry_t *)slab_alloc(NULL,
				    sizeof (rabin_blockentry_t));
			}
			ctx->blocks[i]->offset = last_offset;
			ctx->blocks[i]->index = i; // Need to store for sorting
			ctx->blocks[i]->length = length;
			ctx->blocks[i]->similar = 0;
			ctx->blocks[i]->hash = XXH_fast32(buf1+last_offset, length, 0);
			ctx->blocks[i]->similarity_hash = ctx->blocks[i]->hash;
			last_offset += length;
		}
		goto process_blocks;
	}

	if (rabin_pos == NULL) {
		/*
		 * Initialize arrays for sketch computation. We re-use memory allocated
		 * for the compressed chunk temporarily.
		 */
		ary_sz = 4 * ctx->rabin_poly_max_block_size;
		fplist = (uint32_t *)(ctx->cbuf + ctx->real_chunksize - ary_sz);
		memset(fplist, 0, ary_sz);
	}
	memset(ctx->current_window_data, 0, RAB_POLYNOMIAL_WIN_SIZE);

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
		ssize_t pc[4];
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
				ctx->blocks[blknum] = (rabin_blockentry_t *)slab_alloc(NULL,
				    sizeof (rabin_blockentry_t));
			ctx->blocks[blknum]->offset = last_offset;
			ctx->blocks[blknum]->index = blknum; // Need to store for sorting
			ctx->blocks[blknum]->length = length;

			/*
			 * Reset the heap structure and find the K min values if Delta Compression
			 * is enabled. We use a min heap mechanism taken from the heap based priority
			 * queue implementation in Python.
			 * Here K = 60% or 40%. We are aiming to detect either 60% (default) or 40%
			 * similarity on average.
			 */
			if (ctx->delta_flag) {
				pc[1] = SIXTY_PCNT(j);
				pc[2] = FIFTY_PCNT(j);
				pc[3] = FORTY_PCNT(j);

				reset_heap(&heap, pc[ctx->delta_flag]);
				ksmallest(fplist, j, &heap);
				ctx->blocks[blknum]->similarity_hash =
					XXH_fast32((const uchar_t *)fplist,  pc[ctx->delta_flag]*4, 0);
				memset(fplist, 0, ary_sz);
			}
			blknum++;
			last_offset = i+1;
			length = 0;
			j = 0;
		}
	}

	// Insert the last left-over trailing bytes, if any, into a block.
	if (last_offset < *size) {
		if (ctx->blocks[blknum] == 0)
			ctx->blocks[blknum] = (rabin_blockentry_t *)slab_alloc(NULL,
				sizeof (rabin_blockentry_t));
		ctx->blocks[blknum]->offset = last_offset;
		ctx->blocks[blknum]->index = blknum;
		ctx->blocks[blknum]->length = *size - last_offset;

		if (ctx->delta_flag) {
			uint64_t cur_sketch;
			ssize_t pc[3];

			if (j > 1) {
				pc[1] = SIXTY_PCNT(j);
				pc[2] = FIFTY_PCNT(j);
				pc[3] = FORTY_PCNT(j);
				reset_heap(&heap, pc[ctx->delta_flag]);
				ksmallest(fplist, j, &heap);
				cur_sketch =
				    XXH_fast32((const uchar_t *)fplist,  pc[ctx->delta_flag]*4, 0);
			} else {
				if (j == 0) j = 1;
				cur_sketch =
				    XXH_fast32((const uchar_t *)fplist, (j*4)/2, 0);
			}
			ctx->blocks[blknum]->similarity_hash = cur_sketch;
		}

		blknum++;
		last_offset = *size;
	}

process_blocks:
	// If we found at least a few chunks, perform dedup.
	DEBUG_STAT_EN(printf("Original size: %lld, blknum: %u\n", *size, blknum));
	if (blknum > 2) {
		ssize_t pos, matchlen, pos1;
		int valid = 1;
		uint32_t *dedupe_index;
		ssize_t dedupe_index_sz;
		rabin_blockentry_t *be;
		DEBUG_STAT_EN(uint32_t delta_calls, delta_fails, merge_count, hash_collisions);
		DEBUG_STAT_EN(delta_calls = 0);
		DEBUG_STAT_EN(delta_fails = 0);
		DEBUG_STAT_EN(hash_collisions = 0);

		ary_sz = blknum * sizeof (rabin_blockentry_t *);
		htab = (rabin_blockentry_t **)(ctx->cbuf + ctx->real_chunksize - ary_sz);
		memset(htab, 0, ary_sz);

		/*
		 * Compute hash signature for each block. We do this in a separate loop to 
		 * have a fast linear scan through the buffer.
		 */
		if (ctx->delta_flag) {
			for (i=0; i<blknum; i++) {
				ctx->blocks[i]->hash = XXH_fast32(buf1+ctx->blocks[i]->offset,
								    ctx->blocks[i]->length, 0);
			}
		} else {
			for (i=0; i<blknum; i++) {
				ctx->blocks[i]->hash = XXH_fast32(buf1+ctx->blocks[i]->offset,
								    ctx->blocks[i]->length, 0);
				ctx->blocks[i]->similarity_hash = ctx->blocks[i]->hash;
			}
		}

		/*
		 * Perform hash-matching of blocks and use a bucket-chained hashtable to match
		 * for duplicates and similar blocks. Unique blocks are inserted and duplicates
		 * and similar ones are marked in the block array.
		 *
		 * Hashtable memory is not allocated. We just use available space in the
		 * target buffer.
		 */
		matchlen = 0;
		for (i=0; i<blknum; i++) {
			uint64_t ck;

			/*
			 * Bias hash with length for fewer collisions. If Delta Compression is
			 * not enabled then value of similarity_hash == hash.
			 */
			ck = ctx->blocks[i]->similarity_hash;
			ck += (ck / ctx->blocks[i]->length);
			j = ck % blknum;

			if (htab[j] == 0) {
				/*
				 * Hash bucket empty. So add block into table.
				 */
				htab[j] = ctx->blocks[i];
				ctx->blocks[i]->other = 0;
				ctx->blocks[i]->next = 0;
				ctx->blocks[i]->similar = 0;
			} else {
				be = htab[j];
				length = 0;

				/*
				 * Look for exact duplicates. Same cksum, length and memcmp()\
				 */
				while (1) {
					if (be->hash == ctx->blocks[i]->hash &&
					    be->length == ctx->blocks[i]->length &&
					    memcmp(buf1 + be->offset, buf1 + ctx->blocks[i]->offset,
					    be->length) == 0) {
						ctx->blocks[i]->similar = SIMILAR_EXACT;
						ctx->blocks[i]->other = be;
						be->similar = SIMILAR_REF;
						matchlen += be->length;
						length = 1;
						break;
					}
					if (be->next)
						be = be->next;
					else
						break;
				}

				if (!length && ctx->delta_flag) {
					/*
					 * Look for similar blocks.
					 */
					be = htab[j];
					while (1) {
						if (be->similarity_hash == ctx->blocks[i]->similarity_hash &&
						    be->length == ctx->blocks[i]->length) {
							ctx->blocks[i]->similar = SIMILAR_PARTIAL;
							ctx->blocks[i]->other = be;
							be->similar = SIMILAR_REF;
							matchlen += (be->length>>1);
							length = 1;
							break;
						}
						if (be->next)
							be = be->next;
						else
							break;
					}
				}
				/*
				 * No duplicate in table for this block. So add it to
				 * the bucket chain.
				 */
				if (!length) {
					ctx->blocks[i]->other = 0;
					ctx->blocks[i]->next = 0;
					ctx->blocks[i]->similar = 0;
					be->next = ctx->blocks[i];
					DEBUG_STAT_EN(hash_collisions++);
				}
			}
		}
		DEBUG_STAT_EN(printf("Total Hashtable bucket collisions: %u\n", hash_collisions));

		dedupe_index_sz = (ssize_t)blknum * RABIN_ENTRY_SIZE;
		if (matchlen < dedupe_index_sz) {
			ctx->valid = 0;
			return;
		}

		dedupe_index = (uint32_t *)(ctx->cbuf + RABIN_HDR_SIZE);
		pos = 0;
		DEBUG_STAT_EN(merge_count = 0);

		/*
		 * Merge runs of unique blocks into a single block entry to reduce
		 * dedupe index size.
		 */
		for (i=0; i<blknum;) {
			dedupe_index[pos] = i;
			ctx->blocks[i]->index = pos;
			pos++;
			length = 0;
			j = i;
			if (ctx->blocks[i]->similar == 0) {
				while (i< blknum && ctx->blocks[i]->similar == 0 &&
				   length < RABIN_MAX_BLOCK_SIZE) {
					length += ctx->blocks[i]->length;
					i++;
					DEBUG_STAT_EN(merge_count++);
				}
				ctx->blocks[j]->length = length;
			} else {
				i++;
			}
		}
		DEBUG_STAT_EN(printf("Merge count: %u\n", merge_count));

		/*
		 * Final pass update dedupe index and copy data.
		 */
		blknum = pos;
		dedupe_index_sz = (ssize_t)blknum * RABIN_ENTRY_SIZE;
		pos1 = dedupe_index_sz + RABIN_HDR_SIZE;
		matchlen = ctx->real_chunksize - *size;
		for (i=0; i<blknum; i++) {
			be = ctx->blocks[dedupe_index[i]];
			if (be->similar == 0 || be->similar == SIMILAR_REF) {
				/* Just copy. */
				dedupe_index[i] = htonl(be->length);
				memcpy(ctx->cbuf + pos1, buf1 + be->offset, be->length);
				pos1 += be->length;
			} else {
				if (be->similar == SIMILAR_EXACT) {
					dedupe_index[i] = htonl((be->other->index | RABIN_INDEX_FLAG) &
					    CLEAR_SIMILARITY_FLAG);
				} else {
					uchar_t *old, *new;
					int32_t bsz;
					/*
					 * Perform bsdiff.
					 */
					old = buf1 + be->other->offset;
					new = buf1 + be->offset;
					DEBUG_STAT_EN(delta_calls++);

					bsz = bsdiff(old, be->other->length, new, be->length,
					    ctx->cbuf + pos1, buf1 + *size, matchlen);
					if (bsz == 0) {
						DEBUG_STAT_EN(delta_fails++);
						memcpy(ctx->cbuf + pos1, new, be->length);
						dedupe_index[i] = htonl(be->length);
						pos1 += be->length;
					} else {
						dedupe_index[i] = htonl(be->other->index |
						    RABIN_INDEX_FLAG | SET_SIMILARITY_FLAG);
						pos1 += bsz;
					}
				}
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
			entries[2] = htonll(pos1 - dedupe_index_sz - RABIN_HDR_SIZE);
			*size = pos1;
			ctx->valid = 1;
			DEBUG_STAT_EN(printf("Deduped size: %lld, blknum: %u, delta_calls: %u, delta_fails: %u\n",
					     *size, blknum, delta_calls, delta_fails));
			/*
			 * Remaining header entries: size of compressed index and size of
			 * compressed data are inserted later via rabin_update_hdr, after actual compression!
			 */
			return (dedupe_index_sz);
		}
	}
	return (0);
}

void
update_dedupe_hdr(uchar_t *buf, ssize_t dedupe_index_sz_cmp, ssize_t dedupe_data_sz_cmp)
{
	ssize_t *entries;

	buf += sizeof (uint32_t);
	entries = (ssize_t *)buf;
	entries[1] = htonll(dedupe_index_sz_cmp);
	entries[3] = htonll(dedupe_data_sz_cmp);
}

void
parse_dedupe_hdr(uchar_t *buf, uint32_t *blknum, ssize_t *dedupe_index_sz,
		ssize_t *dedupe_data_sz, ssize_t *dedupe_index_sz_cmp,
		ssize_t *dedupe_data_sz_cmp, ssize_t *deduped_size)
{
	ssize_t *entries;

	*blknum = ntohl(*((uint32_t *)(buf)));
	buf += sizeof (uint32_t);

	entries = (ssize_t *)buf;
	*dedupe_data_sz = ntohll(entries[0]);
	*dedupe_index_sz = (ssize_t)(*blknum) * RABIN_ENTRY_SIZE;
	*dedupe_index_sz_cmp =  ntohll(entries[1]);
	*deduped_size = ntohll(entries[2]);
	*dedupe_data_sz_cmp = ntohll(entries[3]);
}

void
dedupe_decompress(dedupe_context_t *ctx, uchar_t *buf, ssize_t *size)
{
	uint32_t blknum, blk, oblk, len;
	uint32_t *dedupe_index;
	ssize_t data_sz, sz, indx_cmp, data_sz_cmp, deduped_sz;
	ssize_t dedupe_index_sz, pos1, i;
	uchar_t *pos2;

	parse_dedupe_hdr(buf, &blknum, &dedupe_index_sz, &data_sz, &indx_cmp, &data_sz_cmp, &deduped_sz);
	dedupe_index = (uint32_t *)(buf + RABIN_HDR_SIZE);
	pos1 = dedupe_index_sz + RABIN_HDR_SIZE;
	pos2 = ctx->cbuf;
	sz = 0;
	ctx->valid = 1;

	slab_cache_add(sizeof (rabin_blockentry_t));
	for (blk = 0; blk < blknum; blk++) {
		if (ctx->blocks[blk] == 0)
			ctx->blocks[blk] = (rabin_blockentry_t *)slab_alloc(NULL, sizeof (rabin_blockentry_t));
		len = ntohl(dedupe_index[blk]);
		ctx->blocks[blk]->hash = 0;
		if (len == 0) {
			ctx->blocks[blk]->hash = 1;

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

		if (ctx->blocks[blk]->hash == 1) continue;
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
rabin_compress(dedupe_context_t *ctx, uchar_t *from, ssize_t fromlen, uchar_t *to, ssize_t *tolen,
    int level, char chdr, void *data, compress_func_ptr cmp)
{
}
*/
