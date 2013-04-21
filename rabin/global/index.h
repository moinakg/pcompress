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

#ifndef	_INDEX_H
#define	_INDEX_H

#include <dedupe_config.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Publically visible In-memory hashtable entry.
 */
typedef struct _hash_entry {
	uint64_t item_offset;
	uint32_t item_size;
	struct _hash_entry *next;
	uchar_t cksum[1];
} hash_entry_t;

archive_config_t *init_global_db(char *configfile);
int setup_db_config_s(archive_config_t *cfg, uint32_t chunksize, uint64_t *user_chunk_sz,
		 int *pct_interval, const char *algo, cksum_t ck, cksum_t ck_sim,
		 size_t file_sz, uint32_t *hash_slots, int *hash_entry_size,
		 uint64_t *memreqd, size_t memlimit, char *tmppath);
archive_config_t *init_global_db_s(char *path, char *tmppath, uint32_t chunksize,
			uint64_t user_chunk_sz, int pct_interval, const char *algo,
			cksum_t ck, cksum_t ck_sim, size_t file_sz, size_t memlimit,
			int nthreads);
hash_entry_t *db_lookup_insert_s(archive_config_t *cfg, uchar_t *sim_cksum, int interval,
		   uint64_t item_offset, uint32_t item_size, int do_insert);
void destroy_global_db_s(archive_config_t *cfg);

int db_segcache_write(archive_config_t *cfg, int tid, uchar_t *buf, uint32_t len, uint32_t blknum, uint64_t file_offset);
int db_segcache_pos(archive_config_t *cfg, int tid);
int db_segcache_map(archive_config_t *cfg, int tid, uint32_t *blknum, uint64_t *offset, uchar_t **blocks);
int db_segcache_unmap(archive_config_t *cfg, int tid);

#ifdef	__cplusplus
}
#endif

#endif	
