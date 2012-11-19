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

#ifndef	_C_ONFIG_H
#define	_C_ONFIG_H

#include <limits.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	DEFAULT_SIMILARITY_INTERVAL	10
#define	DEFAULT_CKSUM	"SHA256"
#define	CONTAINER_ITEMS 2048
#define	MIN_CK 1
#define	MAX_CK 5

// 8GB
#define	MIN_ARCHIVE_SZ (8589934592ULL)

typedef struct {
	char rootdir[PATH_MAX+1];
	uint32_t chunk_sz; // Numeric ID: 1 - 4k ... 5 - 64k
	uint64_t archive_sz; // Total size of archive in bytes.
	int chunk_cksum_type; // Which digest to use for hash based chunk lookup.
	int similarity_interval; // Similarity based match intervals in %age.
			// The items below are computed given the above
			// components.

	uint32_t chunk_sz_bytes;
	uint32_t segment_sz; // Number of chunks
	uint32_t container_sz; // Number of segments
	int directory_fanout; // Number of subdirectories in a directory
	int directory_levels; // Levels of nested directories
	int num_containers; // Number of containers in a directory
} archive_config_t;

int read_config(char *configfile, archive_config_t *cfg);

#ifdef	__cplusplus
}
#endif

#endif	
