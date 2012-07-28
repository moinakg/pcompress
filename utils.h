/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012 Moinak Ghosh. All rights reserved.
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
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 * This program includes partly-modified public domain source
 * code from the LZMA SDK: http://www.7-zip.org/sdk.html
 */

#ifndef	_UTILS_H
#define	_UTILS_H

#include <arpa/nameser_compat.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	DATA_TEXT	1
#define	DATA_BINARY	2

#if !defined(sun) && !defined(__sun)
#define ulong_t u_long
#define uchar_t u_char
#define uint8_t u_char
#define uint64_t u_int64_t
#define uint32_t u_int32_t
#endif

#if ULONG_MAX == 4294967295UL
#       ifndef UINT64_C
#               define UINT64_C(n) n ## ULL
#       endif
#else
#       ifndef UINT64_C
#               define UINT64_C(n) n ## UL
#       endif
#endif
typedef unsigned long uintptr_t;
typedef ssize_t bsize_t;

#undef WORDS_BIGENDIAN
#if BYTE_ORDER == BIG_ENDIAN
#	define WORDS_BIGENDIAN
#	ifndef htonll
#		define htonll(x) (x)
#	endif
#	ifndef ntonll
#		define ntohll(x) (x)
#	endif
#else
#	if !defined(sun) && !defined (__sun)
#	ifndef htonll
#		define htonll(x) __bswap_64(x)
#	endif
#	ifndef ntohll
#		define ntohll(x) __bswap_64(x)
#	endif
#	endif
#endif


// These allow helping the compiler in some often-executed branches, whose
// result is almost always the same.
#ifdef __GNUC__
#       define likely(expr) __builtin_expect(expr, 1)
#       define unlikely(expr) __builtin_expect(expr, 0)
#	define ATOMIC_ADD(var, val) __sync_fetch_and_add(&var, val)
#	define ATOMIC_SUB(var, val) __sync_fetch_and_sub(&var, val)
#else
#       define likely(expr) (expr)
#       define unlikely(expr) (expr)
#	if defined(sun) || defined (__sun)
#		include <atomic.h>
#		define ATOMIC_ADD(var, val) atomic_add_int(&var, val)
#		define ATOMIC_SUB(var, val) atomic_add_int(&var, -val)
#	else
// Dunno what to do
#		define ATOMIC_ADD(var, val) var += val
#		define ATOMIC_SUB(var, val) var -= val
#	endif
#endif

#define ISP2(x) ((x != 0) && ((x & (~x + 1)) == x))

extern void err_exit(int show_errno, const char *format, ...);
extern const char *get_execname(const char *);
extern int parse_numeric(ssize_t *val, const char *str);
extern char *bytes_to_size(uint64_t bytes);
extern uint32_t hash6432shift(uint64_t key);
extern ssize_t Read(int fd, void *buf, size_t count);
extern ssize_t Read_Adjusted(int fd, uchar_t *buf, size_t count,
	ssize_t *rabin_count, void *ctx);
extern ssize_t Write(int fd, const void *buf, size_t count);

/* Pointer type for compress and decompress functions. */
typedef int (*compress_func_ptr)(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, uchar_t chdr, void *data);

/* Pointer type for algo specific init/deinit/stats functions. */
typedef int (*init_func_ptr)(void **data, int *level, ssize_t chunksize);
typedef int (*deinit_func_ptr)(void **data);
typedef void (*stats_func_ptr)(int show);


/*
 * Roundup v to the nearest power of 2. From Bit Twiddling Hacks:
 * http://graphics.stanford.edu/~seander/bithacks.html
 */
static inline unsigned int
roundup_pow_two(unsigned int v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return (v);
}

#ifdef	__cplusplus
}
#endif

#endif	
