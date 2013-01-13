#ifndef __HEAPQ_H_

#define __TYPE int64_t

typedef struct {
    __TYPE *ary;
    __TYPE len;
    __TYPE tot;
} heap_t;

extern int ksmallest(__TYPE *ary, __TYPE len, heap_t *heap);
extern void reset_heap(heap_t *h, __TYPE tot);

#endif
