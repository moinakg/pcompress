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

#ifndef	_UTILS_H
#define	_UTILS_H

#include <arpa/nameser_compat.h>
#include <arpa/inet.h>
#include <sys/types.h>

#ifndef __STDC_FORMAT_MACROS
#define	__STDC_FORMAT_MACROS	1
#endif

#include <inttypes.h>
#include <stdint.h>
#include <assert.h>
#include <cpuid.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	DATA_TEXT	1
#define	DATA_BINARY	2
#define	EIGHTM		(8UL * 1024UL * 1024UL)
#define	FOURM		(4UL * 1024UL * 1024UL)

#define	ONE_PB (1125899906842624ULL)
#define	ONE_TB (1099511627776ULL)
#define	TWO_MB (2UL * 1024UL * 1024UL)
#define	FOUR_MB FOURM
#define	EIGHT_MB EIGHTM
#define	EIGHT_GB (8589934592ULL)
#define	SIXTEEN_GB (EIGHT_GB * 2)

#if !defined(sun) && !defined(__sun)
#define uchar_t u_char
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

#ifndef UINT64_MAX
#define	UINT64_MAX (18446744073709551615ULL)
#endif

typedef unsigned long uintptr_t;
typedef int32_t bsize_t;

#undef WORDS_BIGENDIAN
#if BYTE_ORDER == BIG_ENDIAN
#	define WORDS_BIGENDIAN
#	ifndef htonll
#		define htonll(x) (x)
#	endif
#	ifndef ntonll
#		define ntohll(x) (x)
#	endif
#	if !defined(sun) && !defined (__sun)
#		define LE64(x) __bswap_64(x)
#		define LE32(x) __bswap_32(x)
#	else
#		define LE64(x) BSWAP_64(x)
#		define LE32(x) BSWAP_32(x)
#	endif
#else
#	if !defined(sun) && !defined (__sun)
#		ifndef htonll
#			define htonll(x) __bswap_64(x)
#		endif
#		ifndef ntohll
#			define ntohll(x) __bswap_64(x)
#		endif
#	endif
#	define LE64(x) (x)
#	define LE32(x) (x)
#endif


// These allow helping the compiler in some often-executed branches, whose
// result is almost always the same.
#ifdef __GNUC__
#	define	likely(expr) __builtin_expect(expr, 1)
#	define	unlikely(expr) __builtin_expect(expr, 0)
#	define	ATOMIC_ADD(var, val) __sync_fetch_and_add(&var, val)
#	define	ATOMIC_SUB(var, val) __sync_fetch_and_sub(&var, val)
#	define	PREFETCH_WRITE(x, n) __builtin_prefetch((x), 1, (n))
#	define	PREFETCH_READ(x, n)  __builtin_prefetch((x), 0, (n))
#	define	NOINLINE_ATTR __attribute__((noinline))
#else
#       define likely(expr) (expr)
#       define unlikely(expr) (expr)
#	define	PREFETCH_WRITE(x, n)
#	define	PREFETCH_READ(x, n)
#	define	NOINLINE_ATTR
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

#ifdef __GNUC__
#define DLL_EXPORT __attribute__ ((visibility ("default")))
#define DLL_LOCAL  __attribute__ ((visibility ("hidden")))
#else
#define DLL_EXPORT
#define DLL_LOCAL
#endif

#define ISP2(x) ((x != 0) && ((x & (~x + 1)) == x))

#ifdef	DEBUG_STATS
#define	DEBUG_STAT_EN(...) __VA_ARGS__;
#else
#define	DEBUG_STAT_EN(...)
#endif

#define	BYTES_TO_MB(x) ((x) / (1024 * 1024))

/*
 * Public checksum properties. CKSUM_MAX_BYTES must be updated if a
 * newer larger checksum is added to the list.
 */
typedef enum {
	CKSUM_CRC64 = 0x100,
	CKSUM_BLAKE256 = 0x200,
	CKSUM_BLAKE512 = 0x300,
	CKSUM_SHA256 = 0x400,
	CKSUM_SHA512 = 0x500,
	CKSUM_KECCAK256 = 0x600,
	CKSUM_KECCAK512 = 0x700,
/*
 * Backwards compatibility options. SKEIN in release 1.2 was replaced with
 * Blake2 from 1.3 onwards (for sheer speed of Blake2). We want to be able
 * to decode archives created with 1.2. New archives do not use SKEIN.
 * 
 * However SKEIN can be used as a chunk/block hash for Global Deduplication.
 * So it will not be removed.
 */
	CKSUM_SKEIN256 = 0x800,
	CKSUM_SKEIN512 = 0x900,
	CKSUM_INVALID = 0
} cksum_t;

typedef enum {
	COMPRESS_NONE = 0,
	COMPRESS_LZFX,
	COMPRESS_LZ4,
	COMPRESS_ZLIB,
	COMPRESS_BZIP2,
	COMPRESS_LZMA,
	COMPRESS_INVALID
} compress_algo_t;

typedef struct {
	uint32_t buf_extra;
	int compress_mt_capable;
	int decompress_mt_capable;
	int single_chunk_mt_capable;
	int is_single_chunk;
	int nthreads;
	int c_max_threads;
	int d_max_threads;
	int delta2_span;
	int deltac_min_distance;
	cksum_t cksum;
} algo_props_t;

typedef enum {
	COMPRESS_THREADS = 1,
	DECOMPRESS_THREADS
} algo_threads_type_t;

typedef struct{
	int64_t totalram;
	int64_t freeram;
	int64_t totalswap;
	int64_t freeswap;
	int64_t mem_unit;
	int64_t sharedram;
} my_sysinfo;

#ifndef _IN_UTILS_
extern processor_info_t proc_info;
#endif

extern void err_exit(int show_errno, const char *format, ...);
extern void err_print(int show_errno, const char *format, ...);
extern const char *get_execname(const char *);
extern int parse_numeric(int64_t *val, const char *str);
extern char *bytes_to_size(uint64_t bytes);
extern int64_t Read(int fd, void *buf, uint64_t count);
extern int64_t Read_Adjusted(int fd, uchar_t *buf, uint64_t count,
	int64_t *rabin_count, void *ctx);
extern int64_t Write(int fd, const void *buf, uint64_t count);
extern void set_threadcounts(algo_props_t *props, int *nthreads, int nprocs,
	algo_threads_type_t typ);
extern uint64_t get_total_ram();
extern double get_wtime_millis(void);
extern double get_mb_s(uint64_t bytes, double strt, double en);
extern void get_sys_limits(my_sysinfo *msys_info);
extern int chk_dir(char *dir);
extern void init_algo_props(algo_props_t *props);
extern void init_pcompress();

/* Pointer type for compress and decompress functions. */
typedef int (*compress_func_ptr)(void *src, uint64_t srclen, void *dst,
	uint64_t *destlen, int level, uchar_t chdr, void *data);

typedef enum {
	COMPRESS,
	DECOMPRESS
} compress_op_t;

/* Pointer type for algo specific init/deinit/stats functions. */
typedef int (*init_func_ptr)(void **data, int *level, int nthreads, uint64_t chunksize,
			     int file_version, compress_op_t op);
typedef int (*deinit_func_ptr)(void **data);
typedef void (*stats_func_ptr)(int show);
typedef void (*props_func_ptr)(algo_props_t *data, int level, uint64_t chunksize);


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
