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
125,0,0,82,113,0,125,85,113,0,0,7,0,0,125,0,
0,0,7,87,0,0,82,0,0,88,0,7,0,85,125,85,
0,113,0,0,85,0,0,113,0,113,124,125,0,125,0,0,
113,0,11,113,125,0,0,0,0,85,113,85,22,0,0,125,
0,113,0,0,113,0,82,0,125,111,87,88,69,125,113,0,
124,0,7,22,113,22,0,235,0,120,120,125,113,0,74,120,
0,124,87,7,0,127,0,0,11,85,85,146,115,11,183,146,
0,0,88,0,0,85,42,0,171,0,0,0,0,83,0,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

