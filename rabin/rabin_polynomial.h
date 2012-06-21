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

//List of constants, mostly constraints and defaults for various parameters
//to the Rabin Fingerprinting algorithm

#define RAB_POLYNOMIAL_CONST 2
// 1 << RAB_POLYNOMIAL_AVG_BLOCK_SHIFT = Average Rabin Chunk Size
// So we are always looking at power of 2 chunk sizes to avoid doing a modulus
//
// A value of 11 below gives block size of 2048 bytes
//
#define RAB_POLYNOMIAL_AVG_BLOCK_SHIFT 11
#define RAB_POLYNOMIAL_AVG_BLOCK_SIZE (1 << RAB_POLYNOMIAL_AVG_BLOCK_SHIFT)
#define RAB_POLYNOMIAL_AVG_BLOCK_MASK (RAB_POLYNOMIAL_AVG_BLOCK_SIZE - 1)
#define RAB_POLYNOMIAL_WIN_SIZE 32
#define RAB_POLYNOMIAL_MIN_WIN_SIZE 17
#define RAB_POLYNOMIAL_MAX_WIN_SIZE 63

typedef struct {
	unsigned char *current_window_data;
	int window_pos;
	uint64_t cur_roll_checksum;
} rabin_context_t;

extern rabin_context_t *create_rabin_context();
extern void destroy_rabin_context(rabin_context_t *ctx);
extern ssize_t scan_rabin_chunks(rabin_context_t *ctx, void *buf, 
	ssize_t size, ssize_t offset);

