/* Generated File, DO NOT EDIT */
/* table for the mapping for the perfect hash */
#ifndef STANDARD
#include "standard.h"
#endif /* STANDARD */
#ifndef PHASH
#include "phash.h"
#endif /* PHASH */
#ifndef LOOKUPA
#include "lookupa.h"
#endif /* LOOKUPA */

/* small adjustments to _a_ to make values distinct */
ub1 tab[] = {
20,70,0,4,61,76,0,119,0,0,16,4,10,1,61,76,
61,0,0,16,1,61,0,76,0,123,32,70,28,34,119,51,
0,76,4,122,70,0,0,43,0,106,20,83,0,0,28,66,
79,0,1,47,79,122,0,0,71,75,85,26,0,103,0,76,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>26)^tab[val&0x3f]);
  return rsl;
}

