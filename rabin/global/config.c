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

#include "config.h"
#include "initdb.h"

#define	ONE_PB (1125899906842624ULL)
#define	ONE_TB (1099511627776ULL)
#define	FOUR_MB (4194304ULL)
#define	EIGHT_MB (8388608ULL)

static compress_algo_t
get_compress_level(compress_algo_t algo)
{
	switch (algo) {
		case COMPRESS_NONE:
			return (0);
		case COMPRESS_LZFX:
			return (5);
		case COMPRESS_LZ4:
			return (1);
		case COMPRESS_ZLIB:
		case COMPRESS_BZIP2:
		case COMPRESS_LZMA:
			return (6);
	};
	return (0);
}

static int
get_compress_algo(char *algo_name)
{
	if (strcmp(algo_name, "none") == 0) {
		return (COMPRESS_NONE);

	} else if (strcmp(algo_name, "lzfx") == 0) {
		return (COMPRESS_LZFX);

	} else if (strcmp(algo_name, "lz4") == 0) {
		return (COMPRESS_LZ4);

	} else if (strcmp(algo_name, "zlib") == 0) {
		return (COMPRESS_ZLIB);

	} else if (strcmp(algo_name, "bzip2") == 0) {
		return (COMPRESS_BZIP2);

	} else if (strcmp(algo_name, "lzma") == 0) {
		return (COMPRESS_LZMA);
	}
	return (COMPRESS_INVALID);
}

static char *
get_compress_str(compress_algo_t algo)
{
	if (algo == COMPRESS_NONE) {
		return ("none");

	} else if (algo == COMPRESS_LZFX) {
		return ("lzfx");

	} else if (algo == COMPRESS_LZ4) {
		return ("lz4");

	} else if (algo == COMPRESS_ZLIB) {
		return ("zlib");

	} else if (algo == COMPRESS_BZIP2) {
		return ("bzip2");

	} else if (algo == COMPRESS_LZMA) {
		return ("lzma");
	}
	return ("invalid");
}

static cksum_t
get_cksum_type(char *cksum_name)
{
	if (strcmp(cksum_name, "SHA256") == 0) {
		return (CKSUM_SHA256);

	} else if (cksum_name, "SHA512") == 0) {
		return (CKSUM_SHA512);

	} else if (cksum_name, "BLAKE256") == 0) {
		return (CKSUM_BLAKE256);

	} else if (cksum_name, "BLAKE512") == 0) {
		return (CKSUM_BLAKE512);

	} else if (cksum_name, "KECCAK256") == 0) {
		return (CKSUM_KECCAK256);

	} else if (cksum_name, "KECCAK512") == 0) {
		return (CKSUM_KECCAK512);
	}
	return (CKSUM_INVALID);
}

static char *
get_cksum_str(chunk_cksum_t ck)
{
	if (ck == CKSUM_SHA256) {
		return ("SHA256");

	} else if (ck == CKSUM_SHA512) {
		return ("SHA512");

	} else if (ck == CKSUM_BLAKE256) {
		return ("BLAKE256");

	} else if (ck == CKSUM_BLAKE512) {
		return ("BLAKE512");

	} else if (ck == CKSUM_KECCAK256) {
		return ("KECCAK256");

	} else if (ck == CKSUM_KECCAK512) {
		return ("KECCAK512");
	}
	return ("INVALID");
}

static int
get_cksum_sz(chunk_cksum_t ck)
{
	if (ck == CKSUM_SHA256 || ck == CKSUM_BLAKE256 || ck == CKSUM_KECCAK256) {
		return (32);

	} else if (ck == CKSUM_SHA512 || ck == CKSUM_BLAKE512 || ck == CKSUM_KECCAK512) {
		return (64);
	}
	return (0);
}

int
read_config(char *configfile, archive_config_t *cfg)
{
	FILE *fh;
	char line[255];
	uint32_t container_sz_bytes, segment_sz_bytes, total_dirs, i;

	// Default
	cfg->verify_chunks = 0;
	cfg->algo = COMPRESS_LZ4;
	cfg->chunk_cksum_type = DEFAULT_CKSUM;
	cfg->pct_interval = DEFAULT_SIMILARITY_INTERVAL;

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
					fprintf(stderr, "Invalid ROOTDIR.\n");
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

		} else if (strncmp(line, "VERIFY") == 0) {
			if (strcmp(pos, "no") == 0) {
				cfg->verify_chunks = 0;

			} else if (strcmp(pos, "yes") == 0) {
				cfg->verify_chunks = 1;
			} else {
				fprintf(stderr, "Invalid VERIFY setting. Must be either yes or no.\n");
				fclose(fh);
				return (1);
			}
		} else if (strncmp(line, "COMPRESS") == 0) {
			cfg->algo = get_compress_algo(pos);
			if (cfg->algo == COMPRESS_INVALID) {
				fprintf(stderr, "Invalid COMPRESS setting.\n");
				fclose(fh);
				return (1);
			}
		} else if (strncmp(line, "CHUNK_CKSUM") == 0) {
			cfg->chunk_cksum_type = get_cksum_type(pos);
			if (cfg->chunk_cksum_type == CKSUM_INVALID) {
				fprintf(stderr, "Invalid CHUNK_CKSUM setting.\n");
				fclose(fh);
				return (1);
			}
		}
	}
	fclose(fh);
	cfg->compress_level = get_compress_level(cfg->algo);
	cfg->chunk_cksum_sz = get_cksum_sz(cfg->chunk_cksum_type);

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

	cfg->segment_sz_bytes = segment_sz_bytes;
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

	return (0);
}

int
write_config(char *configfile, archive_config_t *cfg)
{
	fh = fopen(configfile, "w");
	if (fh == NULL) {
		perror(" ");
		return (1);
	}

	fprintf(fh, "#\n# Autogenerated config file\n# !! DO NOT EDIT !!\n#\n\n");
	fprintf(fh, "ROOTDIR = %s\n", cfg->rootdir);
	fprintf(fh, "CHUNKSZ = %u\n", cfg->chunk_sz;
	fprintf(fh, "ARCHIVESZ = %" PRId64 "\n", cfg->archive_sz);

	if (cfg->verify_chunks)
		fprintf(fh, "VERIFY = yes\n");
	else
		fprintf(fh, "VERIFY = no\n");
	fprintf(fh, "COMPRESS = %s\n", get_compress_str(cfg->algo));
	fprintf(fh, "CHUNK_CKSUM = %s\n", get_cksum_str(cfg->chunk_cksum_type));
	fprintf(fh, "\n");
	fclose(fh);
}

int
set_config_s(archive_config_t *cfg, compress_algo_t algo, cksum_t ck, uint32_t chunksize,
		  size_t file_sz, int pct_interval)
{
	cfg->algo = algo;
	cfg->chunk_cksum_type = ck;
	cfg->compress_level = get_compress_level(cfg->algo);
	cfg->chunk_cksum_sz = get_cksum_sz(cfg->chunk_cksum_type);
	cfg->chunk_sz = chunksize;
	cfg->chunk_sz_bytes = RAB_BLK_AVG_SZ(cfg->chunk_sz);
	cfg->pct_interval = pct_interval;

	cfg->archive_sz = file_sz;
	if (cfg->archive_sz < ONE_TB) {
		segment_sz_bytes = FOUR_MB;

	} else {
		segment_sz_bytes = EIGHT_MB;
	}

	cfg->segment_sz_bytes = segment_sz_bytes;
	cfg->segment_sz = segment_sz_bytes / cfg->chunk_sz_bytes;

	return (0);
}

