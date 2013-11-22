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
125,0,0,220,235,125,82,0,113,0,0,7,0,0,82,0,
0,0,7,124,0,0,82,0,0,125,0,7,0,220,125,120,
0,0,0,0,22,0,0,113,0,113,113,0,0,125,85,0,
113,0,11,113,125,7,0,0,0,40,0,113,85,0,0,125,
0,113,0,0,113,0,125,183,40,27,7,15,58,183,113,0,
124,0,0,22,125,220,0,40,0,87,87,125,113,0,183,125,
0,125,87,7,0,85,0,0,59,229,85,7,135,116,0,146,
0,0,82,0,0,0,200,0,56,125,0,0,61,202,0,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

