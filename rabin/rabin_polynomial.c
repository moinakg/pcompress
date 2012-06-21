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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <allocator.h>
#include <utils.h>

#include "rabin_polynomial.h"

unsigned int rabin_polynomial_max_block_size = RAB_POLYNOMIAL_AVG_BLOCK_SIZE;

/*
 * Initialize the algorithm with the default params. Not thread-safe.
 */
rabin_context_t *
create_rabin_context() {
	rabin_context_t *ctx;
	unsigned char *current_window_data;

	ctx = (rabin_context_t *)slab_alloc(NULL, sizeof (rabin_context_t));
	current_window_data = slab_alloc(NULL, RAB_POLYNOMIAL_WIN_SIZE);
	if(ctx == NULL || current_window_data == NULL) {
		fprintf(stderr,
		    "Could not allocate rabin polynomial context, out of memory\n");
		return (NULL);
	}
 
	memset(current_window_data, 0, RAB_POLYNOMIAL_WIN_SIZE);
	ctx->current_window_data = current_window_data;

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

	ctx->window_pos = 0;
	ctx->cur_roll_checksum = 0;
	return (ctx);
}

void
destroy_rabin_context(rabin_context_t *ctx)
{
	slab_free(NULL, ctx->current_window_data);
	slab_free(NULL, ctx);
}

/**
 * Given a buffer compute all the rabin chunks and return the end offset of the
 * last chunk in the buffer. The last chunk may not end at the buffer end. The
 * bytes till the last chunk end is used as the compression chunk and remaining
 * bytes are carried over to the next chunk.
 */
ssize_t
scan_rabin_chunks(rabin_context_t *ctx, void *buf, ssize_t size, ssize_t offset)
{
	size_t i, length, last_offset;

	length = 0;
	last_offset = 0;

	for (i=offset; i<size; i++) {
		char cur_byte = *((char *)(buf+i));
		uint64_t pushed_out = ctx->current_window_data[ctx->window_pos];
		ctx->current_window_data[ctx->window_pos] = cur_byte;
		/*
		 * We want to do:
		 * cur_roll_checksum = cur_roll_checksum * RAB_POLYNOMIAL_CONST + cur_byte;
		 * cur_roll_checksum -= pushed_out * polynomial_pow;
		 *
		 * However since RAB_POLYNOMIAL_CONST == 2, we use shifts.
		 */
		ctx->cur_roll_checksum = (ctx->cur_roll_checksum << 1) + cur_byte;
		ctx->cur_roll_checksum -= (pushed_out << RAB_POLYNOMIAL_WIN_SIZE);

		ctx->window_pos++;
		length++;

		if (ctx->window_pos == RAB_POLYNOMIAL_WIN_SIZE) // Loop back around
			ctx->window_pos=0;
        
		// If we hit our special value or reached the max block size create a new block
		if ((ctx->cur_roll_checksum & RAB_POLYNOMIAL_AVG_BLOCK_MASK) == RAB_POLYNOMIAL_CONST ||
		    length >= rabin_polynomial_max_block_size) {
			last_offset = i+1;
			length = 0;
		}
	}
	if (last_offset == 0) last_offset = size;

	return last_offset;
}

