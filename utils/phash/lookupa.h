/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 */

/*
------------------------------------------------------------------------------
By Bob Jenkins, September 1996.
lookupa.h, a hash function for table lookup, same function as lookup.c.
Use this code in any way you wish.  Public Domain.  It has no warranty.
Source is http://burtleburtle.net/bob/c/lookupa.h
------------------------------------------------------------------------------
*/

#ifndef STANDARD
#include "standard.h"
#endif

#ifndef LOOKUPA
#define LOOKUPA

#define CHECKSTATE 8
#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (hashsize(n)-1)

ub4  lookup(/*_ ub1 *k, ub4 length, ub4 level _*/);
void checksum(/*_ ub1 *k, ub4 length, ub4 *state _*/);

#endif /* LOOKUPA */
