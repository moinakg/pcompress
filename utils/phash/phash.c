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
125,0,0,87,7,113,82,120,113,0,0,113,0,0,113,125,
0,0,7,113,0,113,0,0,0,7,0,131,0,85,0,22,
0,113,0,0,85,0,0,113,0,113,125,113,0,7,22,0,
82,0,0,113,125,125,0,0,0,0,0,113,22,0,0,125,
0,87,0,0,113,0,125,183,82,0,124,88,40,125,0,0,
124,0,168,125,0,125,0,40,0,82,125,113,113,125,116,0,
0,0,113,85,0,88,0,0,42,27,0,0,0,40,183,61,
0,0,0,0,0,111,17,0,87,125,0,0,166,91,0,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

