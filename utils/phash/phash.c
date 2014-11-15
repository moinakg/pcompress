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
0,0,87,235,113,131,120,120,113,85,0,220,125,0,131,7,
0,0,183,125,82,183,0,131,253,125,125,183,0,7,85,183,
0,0,0,145,183,0,131,113,253,183,0,220,0,7,0,113,
82,0,7,113,125,220,0,0,168,113,0,183,220,183,220,22,
0,183,0,235,113,0,183,82,22,27,125,253,142,124,125,235,
232,131,146,235,146,220,0,235,0,220,220,220,113,220,183,135,
87,125,113,220,220,32,229,97,131,40,0,184,237,113,148,184,
0,0,145,0,241,167,0,145,88,88,184,242,57,135,174,0,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>25)^tab[val&0x7f]);
  return rsl;
}

