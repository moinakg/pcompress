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
125,0,0,82,125,113,82,87,113,0,0,113,0,0,113,125,
0,0,7,113,0,113,0,0,0,125,0,131,0,85,125,22,
0,113,0,0,85,0,0,113,0,113,125,113,0,7,22,0,
82,0,0,113,125,125,0,0,0,0,113,7,74,0,0,125,
0,87,0,0,113,0,125,183,82,0,124,40,40,183,0,0,
124,0,55,85,0,125,0,32,0,82,125,74,0,125,0,0,
0,0,113,113,0,40,0,0,42,27,0,0,0,40,183,4,
0,0,0,0,0,24,0,0,114,124,0,0,103,200,0,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

