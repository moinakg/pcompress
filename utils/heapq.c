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
 * Derived from Python's _heapqmodule.c by way of drastic simplification
 * and a few optimizations.
 */

/* 
 * Original Python _heapqmodule.c implementation was derived directly
 * from heapq.py in Py2.3 which was written by Kevin O'Connor, augmented
 * by Tim Peters, annotated by François Pinard, and converted to C by
 * Raymond Hettinger.
 */

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>
#include <heapq.h>

#ifndef NDEBUG
#define ERROR_CHK
#endif

void
reset_heap(heap_t *heap, __TYPE tot)
{
    if (heap) {
        heap->len = 0;
        heap->tot = tot;
    }
}

static int
_siftdownmax(heap_t *h, __TYPE startpos, __TYPE pos)
{
    __TYPE newitem, parent;
    __TYPE parentpos, *heap;

#ifdef ERROR_CHK
    if (pos >= h->len) {
        fprintf(stderr, "_siftdownmax: index out of range\n");
        return -1;
    }
#endif

    heap = h->ary;
    newitem = heap[pos];
    /* Follow the path to the root, moving parents down until finding
       a place newitem fits. */
    while (pos > startpos){
        parentpos = (pos - 1) >> 1;
        parent = heap[parentpos];
        if (parent < newitem) 
            break;
        heap[pos] = parent;
        pos = parentpos;
    }
    heap[pos] = newitem;
    return 0;
}

static int
_siftupmax(heap_t *h, __TYPE spos, __TYPE epos)
{
    __TYPE endpos, childpos, rightpos;
    __TYPE newitem, *heap, pos;

    endpos = h->len;
    heap = h->ary;
#ifdef ERROR_CHK
    if (spos >= endpos) {
        fprintf(stderr, "_siftupmax: index out of range: %" PRId64 ", len: %" PRId64 "\n", spos, endpos);
        return -1;
    }
#endif

    do {
        pos = spos;
        /* Bubble up the smaller child until hitting a leaf. */
        newitem = heap[pos];
        childpos = (pos << 1) + 1;    /* leftmost child position  */
        while (childpos < endpos) {
            /* Set childpos to index of smaller child.   */
            rightpos = childpos + 1;
            if (rightpos < endpos) {
                if (heap[rightpos] < heap[childpos])
                    childpos = rightpos;
            }
            /* Move the smaller child up. */
            heap[pos] = heap[childpos];
            pos = childpos;
            childpos = (pos << 1) + 1;
        }

        /* The leaf at pos is empty now.  Put newitem there, and and bubble
           it up to its final resting place (by sifting its parents down). */
        heap[pos] = newitem;
#ifdef ERROR_CHK
        if (_siftdownmax(h, spos, pos) == -1)
            return (-1);
#else
        _siftdownmax(h, spos, pos);
#endif
        spos--;
    } while (spos >= epos);
    return (0);
}

static int
_siftupmax_s(heap_t *h, __TYPE spos)
{
    __TYPE endpos, childpos, rightpos;
    __TYPE newitem, *heap, pos;

    endpos = h->len;
    heap = h->ary;
#ifdef ERROR_CHK
    if (spos >= endpos) {
        fprintf(stderr, "_siftupmax: index out of range: %" PRId64 ", len: %" PRId64 "\n", spos, endpos);
        return -1;
    }
#endif

    pos = spos;
    /* Bubble up the smaller child until hitting a leaf. */
    newitem = heap[pos];
    childpos = (pos << 1) + 1;    /* leftmost child position  */
    while (childpos < endpos) {
        /* Set childpos to index of smaller child.   */
        rightpos = childpos + 1;
        if (rightpos < endpos) {
            if (heap[rightpos] < heap[childpos])
                childpos = rightpos;
        }
        /* Move the smaller child up. */
        heap[pos] = heap[childpos];
        pos = childpos;
        childpos = (pos << 1) + 1;
    }

    /* The leaf at pos is empty now.  Put newitem there, and and bubble
       it up to its final resting place (by sifting its parents down). */
    heap[pos] = newitem;
    return (_siftdownmax(h, spos, pos));
}

int
ksmallest(__TYPE *ary, __TYPE len, heap_t *heap)
{
	__TYPE elem, los;
	__TYPE i, *hp, n;
	__TYPE tmp;
	
	n = heap->tot;
	heap->ary = ary;
	hp = ary;
	heap->len = n;

#ifdef ERROR_CHK
	if(_siftupmax(heap, n/2-1, 0) == -1)
		return (-1);
#else
		_siftupmax(heap, n/2-1, 0);
#endif

		los = hp[0];
		for (i = n; i < len; i++) {
			elem = ary[i];
			if (elem >= los) {
				continue;
			}
			
			tmp = hp[0];
			hp[0] = elem;
			ary[i] = tmp;
			#ifdef ERROR_CHK
			if (_siftupmax_s(heap, 0) == -1)
				return (-1);
			#else
				_siftupmax_s(heap, 0);
				#endif
				los = hp[0];
		}
		
		return 0;
}

static int
_siftdown(heap_t *h, __TYPE startpos, __TYPE pos)
{
	__TYPE newitem, parent, *heap;
	__TYPE parentpos;

	heap = h->ary;
#ifdef ERROR_CHK
	if (pos >= h->tot) {
		fprintf(stderr, "_siftdown: index out of range: %" PRId64 ", len: %" PRId64 "\n", pos, h->len);
		return -1;
	}
#endif

	/* Follow the path to the root, moving parents down until finding
	   a place newitem fits. */
	newitem = heap[pos];
	while (pos > startpos){
		parentpos = (pos - 1) >> 1;
		parent = heap[parentpos];
		if (parent < newitem) {
			break;
		}
		heap[pos] = parent;
		pos = parentpos;
	}
	heap[pos] = newitem;
	return (0);
}

static int
_siftup(heap_t *h, __TYPE pos)
{
	__TYPE startpos, endpos, childpos, rightpos;
	__TYPE newitem, *heap;

	endpos = h->tot;
	heap = h->ary;
	startpos = pos;
#ifdef ERROR_CHK
	if (pos >= endpos) {
		fprintf(stderr, "_siftup: index out of range: %" PRId64 ", len: %" PRId64 "\n", pos, endpos);
		return -1;
	}
#endif

	/* Bubble up the smaller child until hitting a leaf. */
	newitem = heap[pos];
	childpos = 2*pos + 1;    /* leftmost child position  */
	while (childpos < endpos) {
		/* Set childpos to index of smaller child.   */
		rightpos = childpos + 1;
		if (rightpos < endpos) {
			if (heap[rightpos] < heap[childpos])
				childpos = rightpos;
		}
		/* Move the smaller child up. */
		heap[pos] = heap[childpos];
		pos = childpos;
		childpos = 2*pos + 1;
	}

	/* The leaf at pos is empty now.  Put newitem there, and and bubble
	   it up to its final resting place (by sifting its parents down). */
	heap[pos] = newitem;
	return _siftdown(h, startpos, pos);
}

void
heapify(heap_t *h, __TYPE *ary)
{
	__TYPE i, n;

	n = h->tot;
	h->ary = ary;

	/* Transform bottom-up.  The largest index there's any point to
	   looking at is the largest with a child index in-range, so must
	   have 2*i + 1 < n, or i < (n-1)/2.  If n is even = 2*j, this is
	   (2*j-1)/2 = j-1/2 so j-1 is the largest, which is n//2 - 1.  If
	   n is odd = 2*j+1, this is (2*j+1-1)/2 = j so j-1 is the largest,
	   and that's again n//2-1.
	*/
	for (i=n/2-1 ; i>=0 ; i--)
		if(_siftup(h, i) == -1)
			break;
}
