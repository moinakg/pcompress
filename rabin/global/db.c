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
	uchar_t cksum[1];
} hash_entry_t;

typedef struct {
	hash_entry_t **tab;
} htab_t;

typedef struct {
	htab_t *list;
	pthread_mutex_t *mlist;
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
	int i;

	if (indx) {
		if (indx->list) {
			for (i = 0; i < indx->intervals; i++) {
				if (indx->list[i].tab)
					free(indx->list[i].tab);
			}
			free(indx->list);
		}
		if (indx->mlist)
			free(indx->mlist);
		free(indx);
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
		int hash_entry_size;
		index_t *indx;

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
		indx = calloc(1, sizeof (index_t));
		if (!indx) {
			free(cfg);
			return (NULL);
		}

		indx->memlimit = memlimit;
		indx->list = (htab_t *)calloc(intervals, sizeof (htab_t));
		indx->mlist = (pthread_mutex_t *)malloc(intervals * sizeof (pthread_mutex_t));
		indx->hash_entry_size = hash_entry_size;
		indx->intervals = intervals;
		indx->hash_slots = hash_slots / intervals;

		for (i = 0; i < intervals; i++) {
			indx->list[i].tab = (hash_entry_t **)calloc(hash_slots / intervals,
							sizeof (hash_entry_t *));
			if (!(indx->list[i].tab)) {
				cleanup_indx(indx);
				free(cfg);
				return (NULL);
			}
			indx->memused += ((hash_slots / intervals) * (sizeof (hash_entry_t *)));
			pthread_mutex_init(&(indx->mlist[i]), NULL);
		}

		strcpy(cfg->rootdir, tmppath);
		strcat(cfg->rootdir, "/.segXXXXXX");
		cfg->seg_fd_w = mkstemp(cfg->rootdir);
		cfg->seg_fd_r = (int *)malloc(sizeof (int) * nthreads);
		if (cfg->seg_fd_w == -1 || cfg->seg_fd_r == NULL) {
			cleanup_indx(indx);
			if (cfg->seg_fd_r)
				free(cfg->seg_fd_r);
			free(cfg);
			return (NULL);
		}

		for (i = 0; i < nthreads; i++) {
			cfg->seg_fd_r[i] = open(cfg->rootdir, O_RDONLY);
		}
		cfg->dbdata = indx;
	}
	return (cfg);
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
		val2 = *((size_t *)v1);
		if (val1 != val2) {
			return (1);
		}
		v1 += sizeof (size_t);
		v2 += sizeof (size_t);
		len += sizeof (size_t);
	} while (len < sz);

	return (0);
}

uint64_t
db_lookup_insert_s(archive_config_t *cfg, uchar_t *sim_cksum, int interval,
		   uint64_t seg_offset, int do_insert)
{
	uint32_t htab_entry;
	index_t *indx = (index_t *)(cfg->dbdata);
	hash_entry_t **htab, *ent, **pent;

	assert(cfg->similarity_cksum_sz && (sizeof (size_t) - 1) == 0);
	htab_entry = XXH32(sim_cksum, cfg->similarity_cksum_sz, 0);
	htab_entry ^= (htab_entry / cfg->similarity_cksum_sz);
	htab_entry = htab_entry % indx->hash_slots;
	htab = indx->list[interval].tab;

	pent = &(htab[htab_entry]);
	pthread_mutex_lock(&(indx->mlist[interval]));
	ent = htab[htab_entry];
	while (ent) {
		if (mycmp(sim_cksum, ent->cksum, cfg->similarity_cksum_sz) == 0) {
			uint64_t off;
			off = ent->seg_offset;
			pthread_mutex_unlock(&(indx->mlist[interval]));
			return (off+1);
		}
		pent = &(ent->next);
		ent = ent->next;
	}
	if (do_insert) {
		if (indx->memused + indx->hash_entry_size >= indx->memlimit - (indx->hash_entry_size << 2)) {
			ent = htab[htab_entry];
			pent = &(htab[htab_entry]);
			htab[htab_entry] = htab[htab_entry]->next;
		} else {
			ent = (hash_entry_t *)malloc(indx->hash_entry_size);
		}
		ent->seg_offset = seg_offset;
		ent->next = 0;
		memcpy(ent->cksum, sim_cksum, cfg->similarity_cksum_sz);
		*pent = ent;
	}
	pthread_mutex_unlock(&(indx->mlist[interval]));
	return (0);
}
