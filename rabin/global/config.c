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
#include <ctype.h>
#include <sys/stat.h>
#include <rabin_dedup.h>

#include "initdb.h"

#define	ONE_PB (1125899906842624ULL)
#define	ONE_TB (1099511627776ULL)
#define	FOUR_MB (4194304ULL)
#define	EIGHT_MB (8388608ULL)

int
read_config(char *configfile, archive_config_t *cfg)
{
	FILE *fh;
	char line[255];
	uint32_t container_sz_bytes, segment_sz_bytes, total_dirs, i;

	fh = fopen(configfile, "r");
	if (fh == NULL) {
		perror(" ");
		return (1);
	}
	while (fgets(line, 255, fh) != NULL) {
		int pos;

		if (strlen(line) < 9 || line[0] == '#') {
			continue;
		}
		pos = strchr(line, '=');
		if (pos == NULL) continue;

		pos++; // Skip '=' char
		while (isspace(*pos)) pos++;

		if (strncmp(line, "CHUNKSZ", 7) == 0) {
			int ck = atoi(pos);
			if (ck < MIN_CK || ck > MAX_CK) {
				fprintf(stderr, "Invalid Chunk Size: %d\n", ck);
				fclose(fh);
				return (1);
			}
			cfg->chunk_sz = ck;

		} else if (strncmp(line, "ROOTDIR") == 0) {
			struct stat sb;
			if (stat(pos, &sb) == -1) {
				if (errno != ENOENT) {
					perror(" ");
					fprintf(stderr, "Invalid ROOTDIR\n");
					fclose(fh);
					return (1);
				} else {
					memset(cfg->rootdir, 0, PATH_MAX+1);
					strncpy(cfg->rootdir, pos, PATH_MAX);
				}
			} else {
				fprintf(stderr, "Invalid ROOTDIR. It already exists.\n");
				fclose(fh);
				return (1);
			}
		} else if (strncmp(line, "ARCHIVESZ") == 0) {
			int ovr;
			ssize_t arch_sz;
			ovr = parse_numeric(&arch_sz, pos);
			if (ovr == 1) {
				fprintf(stderr, "ARCHIVESZ value too large.\n");
				fclose(fh);
				return (1);
			}
			if (ovr == 2) {
				fprintf(stderr, "Invalid ARCHIVESZ value.\n");
				fclose(fh);
				return (1);
			}
			cfg->archive_sz = arch_sz;
		}
	}
	fclose(fh);

	/*
	 * Now compute the remaining parameters.
	 */
	cfg->chunk_sz_bytes = RAB_BLK_AVG_SZ(cfg->chunk_sz);
	cfg->directory_levels = 2;
	if (cfg->archive_sz < ONE_TB) {
		segment_sz_bytes = FOUR_MB;
		cfg->directory_fanout = 128;

	} else if (cfg->archive_sz < ONE_PB) {
		segment_sz_bytes = EIGHT_MB;
		cfg->directory_fanout = 256;
	} else {
		segment_sz_bytes = EIGHT_MB;
		cfg->directory_fanout = 256;
		cfg->directory_levels = 3;
	}

	cfg->segment_sz = segment_sz_bytes / cfg->chunk_sz_bytes;

	total_dirs = 1;
	for (i = 0; i < cfg->directory_levels; i++)
		total_dirs *= cfg->directory_fanout;

	// Fixed number of segments in a container for now.
	cfg->container_sz = CONTAINER_ITEMS;
	container_sz_bytes = CONTAINER_ITEMS * segment_sz_bytes;

	if (cfg->archive_sz / total_dirs < container_sz)
		cfg->num_containers = 1;
	else
		cfg->num_containers = (cfg->archive_sz / total_dirs) / container_sz + 1;
}
