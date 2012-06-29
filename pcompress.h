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
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef	_PCOMPRESS_H
#define	_PCOMPRESS_H

#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <rabin_polynomial.h>

#define	CHDR_SZ		1
#define	ALGO_SZ		8
#define	MIN_CHUNK	2048
#define	VERSION		2
#define	FLAG_DEDUP	1

#define	COMPRESSED	1
#define	UNCOMPRESSED	0
#define	CHSIZE_MASK	0x80
#define	BZIP2_A_NUM	16
#define	LZMA_A_NUM	32
#define	COMPRESS_LZMA	1
#define	COMPRESS_BZIP2	2
#define	COMPRESS_PPMD	3
#define	CHUNK_FLAG_DEDUP	2
#define	COMP_EXTN	".pz"

/* Pointer type for compress and decompress functions. */
typedef int (*compress_func_ptr)(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, void *data);

/* Pointer type for algo specific init/deinit/stats functions. */
typedef int (*init_func_ptr)(void **data, int *level, ssize_t chunksize);
typedef int (*deinit_func_ptr)(void **data);
typedef void (*stats_func_ptr)(int show);

extern uint64_t lzma_crc64(const uint8_t *buf, size_t size, uint64_t crc);
extern uint64_t lzma_crc64_8bchk(const uint8_t *buf, size_t size,
	uint64_t crc, uint64_t *cnt);

extern int zlib_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, void *data);
extern int lzma_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, void *data);
extern int bzip2_compress(void *src, size_t srclen, void *dst,
	size_t *destlen, int level, void *data);
extern int adapt_compress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);
extern int ppmd_compress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);

extern int zlib_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);
extern int lzma_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);
extern int bzip2_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);
extern int adapt_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);
extern int ppmd_decompress(void *src, size_t srclen, void *dst,
	size_t *dstlen, int level, void *data);

extern int adapt_init(void **data, int *level, ssize_t chunksize);
extern int adapt2_init(void **data, int *level, ssize_t chunksize);
extern int lzma_init(void **data, int *level, ssize_t chunksize);
extern int ppmd_init(void **data, int *level, ssize_t chunksize);
extern int bzip2_init(void **data, int *level, ssize_t chunksize);
extern int zlib_init(void **data, int *level, ssize_t chunksize);

extern int adapt_deinit(void **data);
extern int lzma_deinit(void **data);
extern int ppmd_deinit(void **data);

extern void adapt_stats(int show);
extern void ppmd_stats(int show);
extern void lzma_stats(int show);
extern void bzip2_stats(int show);
extern void zlib_stats(int show);

/*
 * Per-thread data structure for compression and decompression threads.
 */
struct cmp_data {
	uchar_t *cmp_seg;
	uchar_t *compressed_chunk;
	uchar_t *uncompressed_chunk;
	rabin_context_t *rctx;
	ssize_t rbytes;
	ssize_t chunksize;
	ssize_t len_cmp;
	uint64_t crc64;
	int level;
	unsigned int id;
	compress_func_ptr compress;
	compress_func_ptr decompress;
	int cancel;
	sem_t start_sem;
	sem_t cmp_done_sem;
	sem_t write_done_sem;
	void *data;
	pthread_t thr;
};

#ifdef	__cplusplus
}
#endif

#endif
