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

#include <sys/types.h>
#include <sys/param.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <utils.h>
#include <allocator.h>
#include <pthread.h>
#include <xxhash.h>
#include <sys/mman.h>

#include "index.h"

/*
 * Hashtable structures for in-memory index.
 */
typedef struct {
	hash_entry_t **tab;
} htab_t;

typedef struct {
	htab_t *list;
	uint64_t memlimit;
	uint64_t memused;
	int hash_entry_size, intervals, hash_slots;
} index_t;

archive_config_t *
init_global_db(char *configfile)
{
	archive_config_t *cfg;
	int rv;

	cfg = calloc(1, sizeof (archive_config_t));
	if (!cfg) {
		fprintf(stderr, "Memory allocation failure\n");
		return (NULL);
	}

	rv = read_config(configfile, cfg);
	if (rv != 0)
		return (NULL);

	return (cfg);
}

void
static cleanup_indx(index_t *indx)
{
	int i, j;

	if (indx) {
		if (indx->list) {
			for (i = 0; i < indx->intervals; i++) {
				if (indx->list[i].tab) {
					for (j=0; j<indx->hash_slots; j++) {
						hash_entry_t *he, *nxt;
						he = indx->list[i].tab[j];
						while (he) {
							nxt = he->next;
							free(he);
							he = nxt;
						}
					}
					free(indx->list[i].tab);
				}
			}
			free(indx->list);
		}
		free(indx);
	}
}

#define	MEM_PER_UNIT(ent_sz) ( (ent_sz + sizeof (hash_entry_t *) + \
		(sizeof (hash_entry_t *)) / 2) + sizeof (hash_entry_t **) )
#define	MEM_REQD(hslots, ent_sz) (hslots * MEM_PER_UNIT(ent_sz))
#define	SLOTS_FOR_MEM(memlimit, ent_sz) (memlimit / MEM_PER_UNIT(ent_sz) - 5)

int
setup_db_config_s(archive_config_t *cfg, uint32_t chunksize, uint64_t *user_chunk_sz,
		 int *pct_interval, const char *algo, cksum_t ck, cksum_t ck_sim,
		 size_t file_sz, uint32_t *hash_slots, int *hash_entry_size,
		 uint64_t *memreqd, size_t memlimit, char *tmppath)
{
	int rv, set_user;

	/*
	 * file_sz = 0 and pct_interval = 0 means we are in pipe mode and want a simple
	 * index. Set pct_interval to 100 to indicate that we need to use all of memlimit
	 * for the simple index.
	 * 
	 * If file_sz != 0 but pct_interval = 0 then we need to create a simple index
	 * sized for the given file. If the available memory is not sufficient for a full
	 * index and required index size is 1.25x of availble mem then switch to a
	 * segmented index.
	 * 
	 * If file_sz != 0 and pct_interval != 0 then we explicitly want to create a segmented
	 * index. This option is auto-selected to support the previous behavior.
	 * 
	 * If file_sz = 0 and pct_interval != 0 then we are in pipe mode and want a segmented
	 * index. This is typically for WAN deduplication of large data transfers.
	 */
	if (pct_interval != 0)
		set_user = 0;
	else
		set_user = 1;
	if (file_sz == 0 && *pct_interval == 0)
		*pct_interval = 100;

set_cfg:
	rv = set_config_s(cfg, algo, ck, ck_sim, chunksize, file_sz, *user_chunk_sz, *pct_interval);

	if (cfg->dedupe_mode == MODE_SIMPLE) {
		if (*pct_interval != 100)
			*pct_interval = 0;
		cfg->pct_interval = 0;
	}

	/*
	 * Adjust user_chunk_sz if indicated.
	 */
	if (set_user) {
		if (*user_chunk_sz < cfg->segment_sz_bytes) {
			*user_chunk_sz = cfg->segment_sz_bytes + (cfg->segment_sz_bytes >> 1);
		} else {
			*user_chunk_sz = (*user_chunk_sz / cfg->segment_sz_bytes) * cfg->segment_sz_bytes;
		}
	}

	// Compute total hashtable entries first
	*hash_entry_size = sizeof (hash_entry_t) + cfg->similarity_cksum_sz - 1;
	if (*pct_interval == 0) {
		cfg->sub_intervals = 1;
		*hash_slots = file_sz / cfg->chunk_sz_bytes + 1;

	} else if (*pct_interval == 100) {
		cfg->sub_intervals = 1;
		*hash_slots = SLOTS_FOR_MEM(memlimit, *hash_entry_size);
		*pct_interval = 0;
	} else {
		cfg->intervals = 100 / *pct_interval;
		cfg->sub_intervals = cfg->intervals;
		*hash_slots = file_sz / cfg->segment_sz_bytes;
		*hash_slots *= cfg->sub_intervals;
	}

	/*
	 * Compute memory required to hold all hash entries assuming worst case 50%
	 * occupancy.
	 */
	*memreqd = MEM_REQD(*hash_slots, *hash_entry_size);

	/*
	 * If memory required is more than thrice the indicated memory limit then
	 * we switch to Segmented Similarity based dedupe.
	 */
	if (*memreqd > (memlimit * 3) && cfg->dedupe_mode == MODE_SIMPLE &&
	    *pct_interval == 0 && tmppath != NULL) {
		*pct_interval = DEFAULT_PCT_INTERVAL;
		set_user = 1;
		goto set_cfg;
	}
	return (rv);
}

archive_config_t *
init_global_db_s(char *path, char *tmppath, uint32_t chunksize, uint64_t user_chunk_sz,
		 int pct_interval, const char *algo, cksum_t ck, cksum_t ck_sim,
		 size_t file_sz, size_t memlimit, int nthreads)
{
	archive_config_t *cfg;
	int rv, orig_pct;
	uint32_t hash_slots, intervals, i;
	uint64_t memreqd;
	int hash_entry_size;
	index_t *indx;

	if (path != NULL) {
		fprintf(stderr, "Disk based index not yet implemented.\n");
		return (NULL);
	}
	orig_pct = pct_interval;
	cfg = calloc(1, sizeof (archive_config_t));

	rv = setup_db_config_s(cfg, chunksize, &user_chunk_sz, &pct_interval, algo, ck, ck_sim,
		 file_sz, &hash_slots, &hash_entry_size, &memreqd, memlimit, tmppath);

	// Reduce hash_slots to remain within memlimit
	while (memreqd > memlimit) {
		hash_slots--;
		memreqd = hash_slots * MEM_PER_UNIT(hash_entry_size);
	}

	/*
	 * Now create as many hash tables as there are similarity match intervals
	 * each having hash_slots / intervals slots.
	 */
	indx = calloc(1, sizeof (index_t));
	if (!indx) {
		free(cfg);
		return (NULL);
	}

	cfg->nthreads = nthreads;
	if (cfg->dedupe_mode == MODE_SIMILARITY)
		intervals = 1;
	else
		intervals = cfg->sub_intervals;
	indx->memlimit = memlimit - (hash_entry_size << 2);
	indx->list = (htab_t *)calloc(intervals, sizeof (htab_t));
	indx->hash_entry_size = hash_entry_size;
	indx->intervals = intervals;
	indx->hash_slots = hash_slots / intervals;

	for (i = 0; i < intervals; i++) {
		indx->list[i].tab = (hash_entry_t **)calloc(indx->hash_slots, sizeof (hash_entry_t *));
		if (!(indx->list[i].tab)) {
			cleanup_indx(indx);
			free(cfg);
			return (NULL);
		}
		indx->memused += ((indx->hash_slots) * (sizeof (hash_entry_t *)));
	}

	if (pct_interval > 0) {
		strcpy(cfg->rootdir, tmppath);
		strcat(cfg->rootdir, "/.segXXXXXX");
		cfg->seg_fd_w = mkstemp(cfg->rootdir);
		cfg->seg_fd_r = (struct seg_map_fd *)malloc(sizeof (struct seg_map_fd) * nthreads);
		if (cfg->seg_fd_w == -1 || cfg->seg_fd_r == NULL) {
			cleanup_indx(indx);
			if (cfg->seg_fd_r)
				free(cfg->seg_fd_r);
			free(cfg);
			return (NULL);
		}
		for (i = 0; i < nthreads; i++) {
			cfg->seg_fd_r[i].fd = open(cfg->rootdir, O_RDONLY);
			cfg->seg_fd_r[i].mapping = NULL;
		}
	}
	cfg->segcache_pos = 0;
	cfg->dbdata = indx;
	return (cfg);
}

/*
 * Functions to handle segment metadata cache for segmented similarity based deduplication.
 * These functions are not thread-safe by design. The caller must ensure thread safety.
 */

/*
 * Add new segment block list array into the metadata cache. Once added the entry is
 * not removed till the program exits.
 */
#define SEGCACHE_HDR_SZ	12
int
db_segcache_write(archive_config_t *cfg, int tid, uchar_t *buf, uint32_t len, uint32_t blknum,
		  uint64_t file_offset)
{
	int64_t w;
	uchar_t hdr[SEGCACHE_HDR_SZ];

	*((uint32_t *)(hdr)) = blknum;
	*((uint64_t *)(hdr + 4)) = file_offset;

	w = Write(cfg->seg_fd_w, hdr, sizeof (hdr));
	if (w < sizeof (hdr))
		return (-1);
	cfg->segcache_pos += w;
	w = Write(cfg->seg_fd_w, buf, len);
	if (w < len)
		return (-1);
	cfg->segcache_pos += w;
	return (0);
}

/*
 * Get the current file pointer position of the metadata file. This indicates the
 * position where the next entry will be added.
 */
int
db_segcache_pos(archive_config_t *cfg, int tid)
{
	return (cfg->segcache_pos);
}

/*
 * Mmap the requested segment metadata array.
 */
int
db_segcache_map(archive_config_t *cfg, int tid, uint32_t *blknum, uint64_t *offset, uchar_t **blocks)
{
	uchar_t *mapbuf, *hdr;
	int fd, dummy;
	uint32_t len, adj;
	uint64_t pos;

	/*
	 * If same mapping is re-attempted just return the pointer into the
	 * existing mapping.
	 */
	adj = *offset % cfg->pagesize;
	if (*offset == cfg->seg_fd_r[tid].cache_offset && cfg->seg_fd_r[tid].mapping) {
		hdr = (uchar_t *)(cfg->seg_fd_r[tid].mapping) + adj;
		*blknum = *((uint32_t *)(hdr));
		*offset = *((uint64_t *)(hdr + 4));
		*blocks = hdr + SEGCACHE_HDR_SZ;
		return (0);
	}

	/*
	 * Ensure previous mapping is removed.
	 */
	db_segcache_unmap(cfg, tid);
	fd = cfg->seg_fd_r[tid].fd;
	if (lseek(fd, *offset, SEEK_SET) != *offset)
		return (-1);

	/*
	 * Mmap hdr and blocks. We assume max # of rabin block entries and mmap (unless remaining
	 * file length is less). The header contains actual number of block entries so mmap-ing
	 * extra has no consequence other than address space usage.
	 */
	len = cfg->segment_sz * sizeof (global_blockentry_t) + SEGCACHE_HDR_SZ;
	pos = cfg->segcache_pos;
	if (pos - *offset < len)
		len = pos - *offset;

	mapbuf = mmap(NULL, len + adj, PROT_READ, MAP_SHARED, fd, *offset - adj);
	if (mapbuf == MAP_FAILED)
		return (-1);

	cfg->seg_fd_r[tid].cache_offset = *offset;
	hdr = mapbuf + adj;
	*blknum = *((uint32_t *)(hdr));
	*offset = *((uint64_t *)(hdr + 4));
	*blocks = hdr + SEGCACHE_HDR_SZ;
	dummy = *(hdr + SEGCACHE_HDR_SZ);

	cfg->seg_fd_r[tid].mapping = mapbuf;
	cfg->seg_fd_r[tid].len = len + adj;

	return (0);
}

/*
 * Remove the metadata mapping.
 */
int
db_segcache_unmap(archive_config_t *cfg, int tid)
{
	if (cfg->seg_fd_r[tid].mapping) {
		munmap(cfg->seg_fd_r[tid].mapping, cfg->seg_fd_r[tid].len);
		cfg->seg_fd_r[tid].mapping = NULL;
	}
	return (0);
}

static inline int
mycmp(uchar_t *a, uchar_t *b, int sz)
{
	size_t val1, val2;
	uchar_t *v1 = a;
	uchar_t *v2 = b;
	int len;

	len = 0;
	do {
		val1 = *((size_t *)v1);
		val2 = *((size_t *)v2);
		if (val1 != val2) {
			return (1);
		}
		v1 += sizeof (size_t);
		v2 += sizeof (size_t);
		len += sizeof (size_t);
	} while (len < sz);

	return (0);
}

/*
 * Lookup and insert item if indicated. Not thread-safe by design. Caller needs to
 * ensure thread-safety.
 */
hash_entry_t *
db_lookup_insert_s(archive_config_t *cfg, uchar_t *sim_cksum, int interval,
		   uint64_t item_offset, uint32_t item_size, int do_insert)
{
	uint32_t htab_entry;
	index_t *indx = (index_t *)(cfg->dbdata);
	hash_entry_t **htab, *ent, **pent;

	assert((cfg->similarity_cksum_sz & (sizeof (size_t) - 1)) == 0);
	htab_entry = XXH32(sim_cksum, cfg->similarity_cksum_sz, 0);
	htab_entry ^= (htab_entry / cfg->similarity_cksum_sz);
	htab_entry = htab_entry % indx->hash_slots;
	htab = indx->list[interval].tab;

	pent = &(htab[htab_entry]);
	ent = htab[htab_entry];
	if (cfg->pct_interval == 0) { // Global dedupe with simple index
		while (ent) {
			if (mycmp(sim_cksum, ent->cksum, cfg->similarity_cksum_sz) == 0 &&
			    ent->item_size == item_size && ent->item_offset != item_offset) {
				return (ent);
			}
			pent = &(ent->next);
			ent = ent->next;
		}
	} else if (cfg->similarity_cksum_sz == 8) {
		while (ent) {
			if (*((uint64_t *)sim_cksum) == *((uint64_t *)ent->cksum) &&
			    ent->item_offset != item_offset) {
				return (ent);
			}
			pent = &(ent->next);
			ent = ent->next;
		}
	} else {
		while (ent) {
			if (mycmp(sim_cksum, ent->cksum, cfg->similarity_cksum_sz) == 0 &&
			    ent->item_offset != item_offset) {
				return (ent);
			}
			pent = &(ent->next);
			ent = ent->next;
		}
	}
	if (do_insert) {
		if (indx->memused + indx->hash_entry_size >= indx->memlimit && htab[htab_entry] != NULL) {
			ent = htab[htab_entry];
			htab[htab_entry] = htab[htab_entry]->next;
		} else {
			ent = (hash_entry_t *)malloc(indx->hash_entry_size);
			indx->memused += indx->hash_entry_size;
		}
		ent->item_offset = item_offset;
		ent->item_size = item_size;
		ent->next = 0;
		memcpy(ent->cksum, sim_cksum, cfg->similarity_cksum_sz);
		*pent = ent;
	}
	return (NULL);
}

void
destroy_global_db_s(archive_config_t *cfg)
{
	int i;
	index_t *indx = (index_t *)(cfg->dbdata);

	cleanup_indx(indx);
	if (cfg->pct_interval > 0) {
		for (i = 0; i < cfg->nthreads; i++) {
			close(cfg->seg_fd_r[i].fd);
		}
		free(cfg->seg_fd_r);
		close(cfg->seg_fd_w);
		unlink(cfg->rootdir);
	}
}

