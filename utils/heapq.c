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
    if (pos >= endpos) {
        fprintf(stderr, "_siftupmax: index out of range: %u, len: %u\n", pos, endpos);
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
    if (pos >= endpos) {
        fprintf(stderr, "_siftupmax: index out of range: %u, len: %u\n", pos, endpos);
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

#ifdef ERROR_CHK
    if (len >= heap->tot) {
        fprintf(stderr, "nsmallest: array size > heap size\n");
        return (-1);
    }
#endif

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

        hp[0] = elem; 
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

