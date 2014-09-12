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
125,0,0,220,85,0,82,87,113,0,0,113,0,0,82,125,
0,0,7,87,0,113,82,0,0,183,0,131,0,7,0,253,
0,0,0,0,85,0,113,0,0,113,125,113,0,7,22,0,
82,0,7,113,125,125,0,0,0,113,113,131,220,0,0,85,
0,87,0,0,113,0,85,183,82,88,7,88,58,113,0,0,
124,0,168,125,0,125,0,116,0,82,125,55,0,22,116,12,
0,125,113,113,0,40,0,0,42,232,0,124,0,92,183,61,
0,0,221,0,0,234,0,0,97,11,0,0,164,91,0,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

