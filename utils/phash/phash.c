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
125,0,0,220,125,0,82,82,113,0,0,113,0,0,113,125,
0,0,7,32,0,113,82,0,0,183,0,131,0,7,220,120,
0,0,0,0,85,0,0,0,0,113,125,113,0,7,22,0,
82,0,7,113,125,125,0,0,0,113,113,85,220,0,0,85,
0,82,0,0,113,0,85,183,82,88,11,85,55,113,0,0,
124,0,113,125,0,125,0,235,0,82,125,55,0,22,0,92,
0,125,113,7,0,40,0,0,82,61,0,42,0,11,177,15,
0,0,0,0,0,6,0,0,56,11,0,0,164,47,0,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

