/*
 * rabin_polynomial_constants.h
 * 
 * Created by Joel Lawrence Tucci on 09-May-2011.
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

#ifndef _RABIN_POLY_H_
#define _RABIN_POLY_H_

#include "utils.h"

//List of constants, mostly constraints and defaults for various parameters
//to the Rabin Fingerprinting algorithm

#define	RAB_POLYNOMIAL_CONST 2
// 1 << RAB_POLYNOMIAL_AVG_BLOCK_SHIFT = Average Rabin Chunk Size
// So we are always looking at power of 2 chunk sizes to avoid doing a modulus
//
#define	RAB_POLYNOMIAL_AVG_BLOCK_SHIFT 12
#define	RAB_POLYNOMIAL_AVG_BLOCK_SIZE (1 << RAB_POLYNOMIAL_AVG_BLOCK_SHIFT)
#define	RAB_POLYNOMIAL_AVG_BLOCK_MASK (RAB_POLYNOMIAL_AVG_BLOCK_SIZE - 1)
#define	RAB_POLYNOMIAL_MIN_BLOCK_SIZE RAB_POLYNOMIAL_AVG_BLOCK_SIZE
#define	RAB_POLYNOMIAL_MAX_BLOCK_SIZE (128 * 1024)

#define	RAB_POLYNOMIAL_AVG_BLOCK_SHIFT2 12
#define	RAB_POLYNOMIAL_AVG_BLOCK_SIZE2 (1 << RAB_POLYNOMIAL_AVG_BLOCK_SHIFT)
#define	RAB_POLYNOMIAL_AVG_BLOCK_MASK2 (RAB_POLYNOMIAL_AVG_BLOCK_SIZE - 1)
#define	RAB_POLYNOMIAL_MIN_BLOCK_SIZE2 2048

#define LZMA_WINDOW_MAX (128L * 1024L * 1024L)
#define	RAB_POLYNOMIAL_WIN_SIZE 16
#define	RAB_POLYNOMIAL_MIN_WIN_SIZE 8
#define	RAB_POLYNOMIAL_MAX_WIN_SIZE 64

// Minimum practical chunk size when doing dedup
#define	RAB_MIN_CHUNK_SIZE (1048576L)

// Number of bytes to compute one maximal fingerprint value
#define	SKETCH_BASIC_BLOCK_SZ (1024)

// An entry in the Rabin block array in the chunk.
// It is either a length value <= RABIN_MAX_BLOCK_SIZE or an index value with
// which this block is a duplicate/similar. The entries are variable sized.
// Offset can be dynamically calculated.
//
#define	RABIN_ENTRY_SIZE (sizeof (unsigned int))

// Header for a chunk deduped using Rabin
// Number of rabin blocks, size of original data chunk, size of compressed index,
// size of deduped data, size of compressed data
#define	RABIN_HDR_SIZE (sizeof (unsigned int) + sizeof (ssize_t) + sizeof (ssize_t) + sizeof (ssize_t) + sizeof (ssize_t))

// Maximum number of dedup blocks supported (2^30 - 1)
#define	RABIN_MAX_BLOCKS (0x3FFFFFFFUL)

// Maximum possible block size for a single rabin block. This is a hard limit much
// larger than RAB_POLYNOMIAL_MAX_BLOCK_SIZE. Useful when merging non-duplicate blocks.
// This is also 2^31 - 1.
#define	RABIN_MAX_BLOCK_SIZE (RABIN_MAX_BLOCKS)

// Masks to determine whether Rabin index entry is a length value, duplicate index value
// or similar index value.
// MSB = 1 : Index
// MSB = 0 : Length
// MSB-1 = 1: Similarity Index
// MSB-1 = 0: Exact Duplicate Index
#define	RABIN_INDEX_FLAG (0x80000000UL)
#define	SET_SIMILARITY_FLAG (0x40000000UL)
#define	GET_SIMILARITY_FLAG SET_SIMILARITY_FLAG
#define	CLEAR_SIMILARITY_FLAG (0xBFFFFFFFUL)

// Mask to extract value from a rabin index entry
#define	RABIN_INDEX_VALUE (0x3FFFFFFFUL)

/*
 * Types of block similarity.
 */
#define	SIMILAR_EXACT 1
#define	SIMILAR_PARTIAL 2

typedef struct {
	ssize_t offset;
	uint64_t cksum_n_offset; // Dual purpose variable
	unsigned int index;
	unsigned int length;
	unsigned int new_length;
	unsigned short refcount;
	short similar;
} rabin_blockentry_t;

typedef struct {
	unsigned char *current_window_data;
	rabin_blockentry_t *blocks;
	unsigned char *cbuf;
	int window_pos;
	uint32_t rabin_poly_max_block_size;
	uint32_t rabin_poly_min_block_size;
	uint32_t rabin_poly_avg_block_size;
	uint32_t rabin_avg_block_mask;
	uint32_t rabin_break_patt;
	uint64_t real_chunksize;
	short valid;
	void *lzma_data;
	int level, delta_flag;
} rabin_context_t;

extern rabin_context_t *create_rabin_context(uint64_t chunksize, uint64_t real_chunksize,
	const char *algo, int delta_flag);
extern void destroy_rabin_context(rabin_context_t *ctx);
extern unsigned int rabin_dedup(rabin_context_t *ctx, unsigned char *buf, 
	ssize_t *size, ssize_t offset, ssize_t *rabin_pos);
extern void rabin_inverse_dedup(rabin_context_t *ctx, uchar_t *buf, ssize_t *size);
extern void rabin_parse_hdr(uchar_t *buf, unsigned int *blknum, ssize_t *rabin_index_sz,
		ssize_t *rabin_data_sz, ssize_t *rabin_index_sz_cmp,
		ssize_t *rabin_data_sz_cmp, ssize_t *rabin_deduped_size);
extern void rabin_update_hdr(uchar_t *buf, ssize_t rabin_index_sz_cmp,
			     ssize_t rabin_data_sz_cmp);
extern void reset_rabin_context(rabin_context_t *ctx);
extern uint32_t rabin_buf_extra(uint64_t chunksize);

#endif /* _RABIN_0POLY_H_ */