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
 * Functions for a rudimentary fast min-heap implementation.
 * Adapted from "Algorithms with C", Kyle Loudon, O'Reilly.
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>
#include "heap.h"

#define	heap_parent(npos) ((__TYPE)(((npos) - 1) / 2))
#define	heap_left(npos) (((npos) * 2) + 1)
#define	heap_right(npos) (((npos) * 2) + 2)

static void
heap_insert(MinHeap *heap, __TYPE data)
{
	__TYPE temp;
	__TYPE ipos, ppos;

	heap->tree[heap_size(heap)] = data;
	ipos = heap_size(heap);
	ppos = heap_parent(ipos);

	while (ipos > 0 && heap->tree[ppos] > heap->tree[ipos]) {
		temp = heap->tree[ppos];
		heap->tree[ppos] = heap->tree[ipos];
		heap->tree[ipos] = temp;
		ipos = ppos;
		ppos = heap_parent(ipos);
	}
	if (heap->size < heap->totsize)
		heap->size++;
}

void
heap_nsmallest(MinHeap *heap, __TYPE *data, __TYPE *heapbuf, __TYPE heapsize, __TYPE datasize)
{
	__TYPE i;

	heap->size = 1;
	heap->totsize = heapsize;
	heap->tree = heapbuf;
	heap->tree[0] = data[0];

	for (i = 1; i < datasize; i++)
		heap_insert(heap, data[i]);
}
