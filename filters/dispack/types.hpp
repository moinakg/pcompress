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
 */

#include <stdint.h>
#include <inttypes.h>
#include <arpa/inet.h>

#ifndef __TYPES_HPP__
#define __TYPES_HPP__

typedef unsigned char             sU8;
typedef signed char               sS8;
typedef unsigned short            sU16;
typedef signed short              sS16;
typedef unsigned int              sU32;
typedef signed int                sS32;
typedef uint64_t                  sU64;
typedef int64_t                   sS64;
typedef int                       sInt;
typedef char                      sChar;
typedef bool                      sBool;
typedef float                     sF32;
typedef double                    sF64;

#define sTRUE                     true
#define sFALSE                    false

#define _byteswap_ushort          htons
#define _byteswap_ulong           htonl
#endif
