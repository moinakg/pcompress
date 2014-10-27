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
0,0,87,120,113,125,22,125,0,0,0,220,125,0,131,7,
0,0,183,125,82,113,0,131,146,87,125,183,0,7,146,183,
0,0,0,253,183,0,131,113,253,168,0,220,0,7,0,113,
82,0,7,131,145,7,0,0,120,113,0,183,220,183,220,22,
0,183,0,183,113,0,183,120,22,27,125,125,233,124,125,235,
253,131,146,235,15,220,0,235,0,235,212,220,220,220,183,132,
87,125,113,82,220,32,229,235,131,27,0,220,237,113,4,132,
0,0,145,0,148,195,0,253,142,88,66,232,137,135,167,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

