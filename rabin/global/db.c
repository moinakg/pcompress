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

#include "db.h"

#define	ONE_PB (1125899906842624ULL)
#define	ONE_TB (1099511627776ULL)
#define	FOUR_MB (4194304ULL)
#define	EIGHT_MB (8388608ULL)

/*
 * Hashtable structures for in-memory index.
 */
typedef struct _hash_entry {
	uint64_t seg_offset;
	struct _hash_entry *next;
	struct _hash_entry *lru_prev;
	struct _hash_entry *lru_next;
	uchar_t cksum[1];
} hash_entry_t;

typedef struct {
	hash_entry_t **htab;
} htab_t;

typedef struct {
	htab_t *list;
	pthread_mutex_t *mlist;
	hash_entry_t *lru_head;
	hash_entry_t *lru_tail;
	uint64_t memlimit;
	uint64_t memused;
	int hash_entry_size, intervals;
} htablst_t;

typedef struct {
	htablst_t *hlist;
	int seg_fd_w;
	int *tfd;
} seg_index_t;

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
static cleanup_htablst(htablst_t *htablst, int intervals)
{
	int i;

	if (htablst) {
		if (htablst->list) {
			for (i = 0; i < intervals; i++) {
				if (htablst->list[i].htab)
					free(htablst->list[i].htab);
			}
			free(htablst->list);
		}
		if (htablst->mlist)
			free(htablst->mlist);
		free(htablst);
	}
}

archive_config_t *
init_global_db_s(char *path, char *tmppath, uint32_t chunksize, int pct_interval,
		 compress_algo_t algo, cksum_t ck, cksum_t ck_sim, size_t file_sz,
		 size_t memlimit, int nthreads)
{
	archive_config_t *cfg;
	int rv;
	float diff;

	cfg = calloc(1, sizeof (archive_config_t));
	rv = set_config_s(cfg, algo, ck, ck_sim, chunksize, file_sz, pct_interval);

	if (path != NULL) {
		printf("Disk based index not yet implemented.\n");
		free(cfg);
		return (NULL);
	} else {
		uint32_t hash_slots, intervals, i;
		uint64_t memreqd;
		htablst_t *htablst;
		int hash_entry_size;
		seg_index_t *indx;

		// Compute total hashtable entries first
		intervals = 100 / pct_interval - 1;
		hash_slots = file_sz / cfg->segment_sz_bytes + 1;
		hash_slots *= intervals;
		hash_entry_size = sizeof (hash_entry_t) + cfg->similarity_cksum_sz - 1;

		// Compute memory required to hold all hash entries assuming worst case 50%
		// occupancy.
		memreqd = hash_slots * (hash_entry_size + sizeof (hash_entry_t *) +
			(sizeof (hash_entry_t *)) / 2);
		memreqd += hash_slots * sizeof (hash_entry_t **);
		diff = (float)pct_interval / 100.0;

		// Reduce hash_slots to remain within memlimit
		while (memreqd > memlimit) {
			hash_slots -= (hash_slots * diff);
			memreqd = hash_slots * (hash_entry_size + sizeof (hash_entry_t *) + 
					(sizeof (hash_entry_t *)) / 2);
			memreqd += hash_slots * sizeof (hash_entry_t **);
		}

		// Now create as many hash tables as there are similarity match intervals
		// each having hash_slots / intervals slots.
		htablst = calloc(1, sizeof (htablst_t));
		if (!htablst) {
			free(cfg);
			return (NULL);
		}

		htablst->memlimit = memlimit;
		htablst->list = (htab_t *)calloc(intervals, sizeof (htab_t));
		htablst->mlist = (pthread_mutex_t *)malloc(intervals * sizeof (pthread_mutex_t));
		htablst->hash_entry_size = hash_entry_size;
		htablst->intervals = intervals;

		for (i = 0; i < intervals; i++) {
			htablst->list[i].htab = (hash_entry_t **)calloc(hash_slots / intervals,
							sizeof (hash_entry_t *));
			if (!(htablst->list[i].htab)) {
				cleanup_htablst(htablst, intervals);
				free(cfg);
				return (NULL);
			}
			htablst->memused += ((hash_slots / intervals) * (sizeof (hash_entry_t *)));
			pthread_mutex_init(&(htablst->mlist[i]), NULL);
		}

		indx = (seg_index_t *)calloc(1, sizeof (seg_index_t));
		if (!indx) {
			cleanup_htablst(htablst, intervals);
			free(cfg);
			return (NULL);
		}
		indx->hlist = htablst;

		strcpy(cfg->rootdir, tmppath);
		strcat(cfg->rootdir, "/.segXXXXXX");
		indx->seg_fd_w = mkstemp(cfg->rootdir);
		indx->tfd = (int *)malloc(sizeof (int) * nthreads);
		if (indx->seg_fd_w == -1 || indx->tfd == NULL) {
			cleanup_htablst(htablst, intervals);
			free(cfg);
			if (indx->tfd)
				free(indx->tfd);
			return (NULL);
		}

		for (i = 0; i < nthreads; i++) {
			indx->tfd[i] = open(cfg->rootdir, O_RDONLY);
		}
		cfg->dbdata = indx;
		slab_cache_add(hash_entry_size);
		slab_cache_add(cfg->chunk_cksum_sz);
	}
	return (cfg);
}

int
db_insert_s(archive_config_t *cfg, uchar_t *sim_cksum, int interval, segment_entry_t *seg, int thr_id)
{
	return (0);
}

segment_entry_t *
db_query_s(archive_config_t *cfg, uchar_t *sim_cksum, int interval, int thr_id)
{
	return (0);
}
