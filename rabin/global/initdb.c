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

#include "initdb.h"
#include "config.h"

#define	ONE_PB (1125899906842624ULL)
#define	ONE_TB (1099511627776ULL)
#define	FOUR_MB (4194304ULL)
#define	EIGHT_MB (8388608ULL)

/*
 * Hashtable structures for in-memory index.
 */
typedef struct _hash_entry {
	uchar_t *cksum;
	struct _hash_entry *next;
	struct _hash_entry *lru_prev;
	struct _hash_entry *lru_next;
} hash_entry_t;

typedef struct {
	hash_entry_t **htab;
} htab_t;

typedef struct {
	htab_t *htablst;
	pthread_mutex_t *mlist;
	hash_entry_t *lru_head;
	hash_entry_t *lru_tail;
	uint64_t memlimit;
	uint64_t memused;
} htablst_t;

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

archive_config_t *
init_global_db_s(char *path, uint32_t chunksize, int pct_interval,
		      compress_algo_t algo, cksum_t ck, size_t file_sz, size_t memlimit)
{
	archive_config_t *cfg;
	int rv;
	float diff;

	cfg = calloc(1, sizeof (archive_config_t));
	rv = set_config_s(cfg, algo, ck, chunksize, file_sz, chunks_per_seg, pct_interval);

	if (path != NULL) {
		printf("Disk based index not yet implemented.\n");
		free(cfg);
		return (NULL);
	} else {
		uint32_t hash_slots, intervals, i;
		uint64_t memreqd;
		htablst_t *htablst;

		// Compute total hashtable entries first
		intervals = 100 / pct_interval - 1;
		hash_slots = file_sz / cfg->segment_sz_bytes + 1;
		hash_slots *= intervals;

		// Compute memory required to hold all hash entries assuming worst case 50%
		// occupancy.
		memreqd = hash_slots * (sizeof (hash_entry_t) + cfg->chunk_cksum_sz +
			sizeof (hash_entry_t *) + (sizeof (hash_entry_t *)) / 2);
		memreqd += hash_slots * sizeof (hash_entry_t **);
		diff = (float)pct_interval / 100.0;

		// Reduce hash_slots to remain within memlimit
		while (memreqd > memlimit) {
			hash_slots -= (hash_slots * diff);
			memreqd = hash_slots * (sizeof (hash_entry_t) +
					cfg->chunk_cksum_sz + sizeof (hash_entry_t *) + 
					(sizeof (hash_entry_t *)) / 2);
			memreqd += hash_slots * sizeof (hash_entry_t **);
		}

		// Now create as many hash tables as there are similarity match intervals
		// each having hash_slots / intervals slots.
		htablst = calloc(1, sizeof (htablst_t));
		htablst->memlimit = memlimit;
		htablst->htablst = (htab_t *)calloc(intervals, sizeof (htab_t));
		htablst->mlist = (pthread_mutex_t *)malloc(intervals * sizeof (pthread_mutex_t));

		for (i = 0; i < intervals; i++) {
			htablst->htablst[i].htab = (hash_entry_t **)calloc(hash_slots / intervals,
							sizeof (hash_entry_t *));
			htablst->memused += ((hash_slots / intervals) * (sizeof (hash_entry_t *)));
			pthread_mutex_init(&(htablst->mlist[i]), NULL);
		}
		cfg->dbdata = htablst;
		slab_cache_add(sizeof (hash_entry_t));
		slab_cache_add(cfg->chunk_cksum_sz);
	}
	return (cfg);
}

int
db_insert_s(archive_config_t *cfg, uchar_t *cksum, int interval_num)
{
}
