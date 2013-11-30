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

#ifndef	_PCOMPRESS_H
#define	_PCOMPRESS_H

#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <rabin_dedup.h>
#include <crypto_utils.h>

#define	CHUNK_FLAG_SZ	1
#define	ALGO_SZ		8
#define	MIN_CHUNK	2048
#define	VERSION		9
#define	FLAG_DEDUP	1
#define	FLAG_DEDUP_FIXED	2
#define	FLAG_SINGLE_CHUNK	4
#define	FLAG_ARCHIVE	2048
#define	UTILITY_VERSION	"2.4"
#define	MASK_CRYPTO_ALG	0x30
#define	MAX_LEVEL	14

#define	COMPRESSED	1
#define	UNCOMPRESSED	0
#define	CHSIZE_MASK	0x80
#define	BZIP2_A_NUM	16
#define	LZMA_A_NUM	32
#define	CHUNK_FLAG_DEDUP		2
#define	CHUNK_FLAG_PREPROC	4
#define	COMP_EXTN	".pz"

#define	PREPROC_TYPE_LZP		1
#define	PREPROC_TYPE_DELTA2	2
#define	PREPROC_TYPE_DISPACK	4
#define	PREPROC_COMPRESSED	128

/*
 * Sizes of chunk header components.
 */
#define	COMPRESSED_CHUNKSZ	(sizeof (uint64_t))
#define	ORIGINAL_CHUNKSZ	(sizeof (uint64_t))
#define	CHUNK_HDR_SZ		(COMPRESSED_CHUNKSZ + pctx->cksum_bytes + ORIGINAL_CHUNKSZ + CHUNK_FLAG_SZ)

/*
 * lower 3 bits in higher nibble indicate chunk compression algorithm
 * in adaptive modes.
 */
#define	ADAPT_COMPRESS_NONE	0
#define	ADAPT_COMPRESS_LZMA	1
#define	ADAPT_COMPRESS_BZIP2	2
#define	ADAPT_COMPRESS_PPMD	3
#define	ADAPT_COMPRESS_BSC	4
/*
 * This is used in adaptive modes in cases where the data is deemed totally incompressible.
 * We can still have zero padding and archive headers that can be compressed. So we use the
 * fastest algo at our disposal for these cases.
 */
#define	ADAPT_COMPRESS_LZ4	5
#define	CHDR_ALGO_MASK	7
#define	CHDR_ALGO(x) (((x)>>4) & CHDR_ALGO_MASK)

extern uint32_t zlib_buf_extra(uint64_t buflen);
extern int lz4_buf_extra(uint64_t buflen);

extern int zlib_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *destlen, int level, uchar_t chdr, int btype, void *data);
extern int lzma_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *destlen, int level, uchar_t chdr, int btype, void *data);
extern int bzip2_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *destlen, int level, uchar_t chdr, int btype, void *data);
extern int adapt_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int ppmd_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int lz_fx_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int lz4_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int none_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);

extern int zlib_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int lzma_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int bzip2_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int adapt_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int ppmd_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int lz_fx_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int lz4_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int none_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);

extern int adapt_init(void **data, int *level, int nthreads, uint64_t chunksize,
		      int file_version, compress_op_t op);
extern int adapt2_init(void **data, int *level, int nthreads, uint64_t chunksize,
		       int file_version, compress_op_t op);
extern int lzma_init(void **data, int *level, int nthreads, uint64_t chunksize,
		     int file_version, compress_op_t op);
extern int ppmd_init(void **data, int *level, int nthreads, uint64_t chunksize,
		     int file_version, compress_op_t op);
extern int bzip2_init(void **data, int *level, int nthreads, uint64_t chunksize,
		      int file_version, compress_op_t op);
extern int zlib_init(void **data, int *level, int nthreads, uint64_t chunksize,
		     int file_version, compress_op_t op);
extern int lz_fx_init(void **data, int *level, int nthreads, uint64_t chunksize,
		      int file_version, compress_op_t op);
extern int lz4_init(void **data, int *level, int nthreads, uint64_t chunksize,
		    int file_version, compress_op_t op);
extern int none_init(void **data, int *level, int nthreads, uint64_t chunksize,
		     int file_version, compress_op_t op);

extern void lzma_props(algo_props_t *data, int level, uint64_t chunksize);
extern void lzma_mt_props(algo_props_t *data, int level, uint64_t chunksize);
extern void lz4_props(algo_props_t *data, int level, uint64_t chunksize);
extern void zlib_props(algo_props_t *data, int level, uint64_t chunksize);
extern void ppmd_props(algo_props_t *data, int level, uint64_t chunksize);
extern void lz_fx_props(algo_props_t *data, int level, uint64_t chunksize);
extern void bzip2_props(algo_props_t *data, int level, uint64_t chunksize);
extern void adapt_props(algo_props_t *data, int level, uint64_t chunksize);
extern void none_props(algo_props_t *data, int level, uint64_t chunksize);

extern int zlib_deinit(void **data);
extern int adapt_deinit(void **data);
extern int lzma_deinit(void **data);
extern int ppmd_deinit(void **data);
extern int lz_fx_deinit(void **data);
extern int lz4_deinit(void **data);
extern int none_deinit(void **data);

extern void adapt_stats(int show);
extern void ppmd_stats(int show);
extern void lzma_stats(int show);
extern void bzip2_stats(int show);
extern void zlib_stats(int show);
extern void lz_fx_stats(int show);
extern void lz4_stats(int show);
extern void none_stats(int show);

#ifdef ENABLE_PC_LIBBSC
extern int libbsc_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int libbsc_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data);
extern int libbsc_init(void **data, int *level, int nthreads, uint64_t chunksize,
	int file_version, compress_op_t op);
extern void libbsc_props(algo_props_t *data, int level, uint64_t chunksize);
extern int libbsc_deinit(void **data);
extern void libbsc_stats(int show);
#endif

typedef struct pc_ctx {
	compress_func_ptr _compress_func;
	compress_func_ptr _decompress_func;
	init_func_ptr _init_func;
	deinit_func_ptr _deinit_func;
	stats_func_ptr _stats_func;
	props_func_ptr _props_func;

	int inited;
	int main_cancel;
	int adapt_mode;
	int pipe_mode, pipe_out;
	int nthreads;
	int hide_mem_stats;
	int hide_cmp_stats;
	int enable_rabin_scan;
	int enable_rabin_global;
	int enable_delta_encode;
	int enable_delta2_encode;
	int enable_rabin_split;
	int enable_fixed_scan;
	int preprocess_mode;
	int lzp_preprocess;
	int dispack_preprocess;
	int encrypt_type;
	int archive_mode;
	int verbose;
	int enable_archive_sort;
	int pagesize;
	int force_archive_perms;
	int no_overwrite_newer;

	/*
	 * Archiving related context data.
	 */
	char archive_members_file[MAXPATHLEN];
	int archive_members_fd;
	uint32_t archive_members_count;
	void *archive_ctx, *archive_sort_buf;
	pthread_t archive_thread;
	char archive_temp_file[MAXPATHLEN];
	int archive_temp_fd;
	uint64_t archive_temp_size, archive_size;
	uchar_t *temp_mmap_buf;
	uint64_t temp_mmap_pos, temp_file_pos;
	uint64_t temp_mmap_len;
	struct fn_list *fn;
	sem_t read_sem, write_sem;
	uchar_t *arc_buf;
	uint64_t arc_buf_size, arc_buf_pos;
	int arc_closed, arc_writing;
	uchar_t btype, ctype;
	int min_chunk;
	int enable_packjpg;

	unsigned int chunk_num;
	uint64_t largest_chunk, smallest_chunk, avg_chunk;
	uint64_t chunksize;
	const char *algo, *filename;
	char *to_filename;
	char *exec_name;
	int do_compress, level;
	int do_uncompress;
	int cksum_bytes, mac_bytes;
	int cksum, t_errored;
	int rab_blk_size, keylen;
	crypto_ctx_t crypto_ctx;
	unsigned char *user_pw;
	int user_pw_len;
	char *pwd_file, *f_name;
} pc_ctx_t;

/*
 * Per-thread data structure for compression and decompression threads.
 */
struct cmp_data {
	uchar_t *cmp_seg;
	uchar_t *compressed_chunk;
	uchar_t *uncompressed_chunk;
	dedupe_context_t *rctx;
	uint64_t rbytes;
	uint64_t chunksize;
	uint64_t len_cmp, len_cmp_be;
	uchar_t checksum[CKSUM_MAX_BYTES];
	int level, cksum_mt, out_fd;
	unsigned int id;
	compress_func_ptr compress;
	compress_func_ptr decompress;
	int cancel;
	sem_t start_sem;
	sem_t cmp_done_sem;
	sem_t write_done_sem;
	sem_t index_sem;
	void *data;
	pthread_t thr;
	mac_ctx_t chunk_hmac;
	algo_props_t *props;
	int decompressing;
	uchar_t btype;
	pc_ctx_t *pctx;
};

void usage(pc_ctx_t *pctx);
pc_ctx_t *create_pc_context(void);
int init_pc_context_argstr(pc_ctx_t *pctx, char *args);
int init_pc_context(pc_ctx_t *pctx, int argc, char *argv[]);
void destroy_pc_context(pc_ctx_t *pctx);
void pc_set_userpw(pc_ctx_t *pctx, unsigned char *pwdata, int pwlen);

int start_pcompress(pc_ctx_t *pctx);
int start_compress(pc_ctx_t *pctx, const char *filename, uint64_t chunksize, int level);
int start_decompress(pc_ctx_t *pctx, const char *filename, char *to_filename);

#ifdef	__cplusplus
}
#endif

#endif
