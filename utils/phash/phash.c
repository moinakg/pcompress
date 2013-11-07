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
10,76,0,76,70,42,0,1,0,0,119,1,61,1,70,79,
0,0,0,4,70,1,0,122,0,119,47,76,76,34,110,101,
0,76,70,70,42,28,0,66,0,108,0,109,28,4,28,4,
70,0,1,20,4,123,123,0,79,75,34,76,69,77,0,69,
};

/* The hash function */
ub4 phash(char *key, int len)
{
  ub4 rsl, val = lookup(key, len, 0x9e3779b9);
  rsl = ((val>>26)^tab[val&0x3f]);
  return rsl;
}

