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
 *      
 */

#include <inttypes.h>
#include <xxhash.h>
#include <pthread.h>
#include <utils.h>

extern void*        XXH32_init_SSE4   (unsigned int seed);
extern int          XXH32_feed_SSE4   (void* state, const void* input, int len);
extern unsigned int XXH32_result_SSE4 (void* state);
extern unsigned int XXH32_getIntermediateResult_SSE4 (void* state);
extern unsigned int XXH32_SSE4 (const void* input, int len, unsigned int seed);

extern void*        XXH32_init_SSE2   (unsigned int seed);
extern int          XXH32_feed_SSE2   (void* state, const void* input, int len);
extern unsigned int XXH32_result_SSE2 (void* state);
extern unsigned int XXH32_getIntermediateResult_SSE2 (void* state);
extern unsigned int XXH32_SSE2 (const void* input, int len, unsigned int seed);

unsigned int (*xxh32)(const void* input, int len, unsigned int seed) = NULL;
void * (*xxh32_init)(unsigned int seed) = NULL;
int (*xxh32_feed)(void* state, const void* input, int len) = NULL;
unsigned int (*xxh32_result)(void* state) = NULL;
unsigned int (*xxh32_getIntermediateResult)(void* state) = NULL;

void
XXH32_module_init() {
	if (proc_info.sse_level >= 4) {
		xxh32 = XXH32_SSE4;
		xxh32_init = XXH32_init_SSE4;
		xxh32_feed = XXH32_feed_SSE4;
		xxh32_result = XXH32_result_SSE4;
		xxh32_getIntermediateResult = XXH32_getIntermediateResult_SSE4;
	} else {
		xxh32 = XXH32_SSE2;
		xxh32_init = XXH32_init_SSE2;
		xxh32_feed = XXH32_feed_SSE2;
		xxh32_result = XXH32_result_SSE2;
		xxh32_getIntermediateResult = XXH32_getIntermediateResult_SSE2;
	}
}

unsigned int
XXH32(const void* input, int len, unsigned int seed)
{
	return xxh32(input, len, seed);
}

void*
XXH32_init(unsigned int seed)
{
	return xxh32_init(seed);
}

int
XXH32_feed(void* state, const void* input, int len)
{
	return xxh32_feed(state, input, len);
}

unsigned int
XXH32_result(void* state)
{
	return xxh32_result(state);
}

unsigned int
XXH32_getIntermediateResult(void* state)
{
	return xxh32_getIntermediateResult(state);
}

