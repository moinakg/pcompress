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

#ifndef	_DB_H
#define	_DB_H

#include <dedupe_config.h>

#ifdef	__cplusplus
extern "C" {
#endif

archive_config_t *init_global_db(char *configfile);
archive_config_t *init_global_db_s(char *path, char *tmppath, uint32_t chunksize,
			uint64_t user_chunk_sz, int pct_interval, compress_algo_t algo,
			cksum_t ck, cksum_t ck_sim, size_t file_sz, size_t memlimit,
			int nthreads);
uint64_t db_lookup_insert_s(archive_config_t *cfg, uchar_t *sim_cksum, int interval,
		   uint64_t item_offset, uint32_t item_size, int do_insert);

#ifdef	__cplusplus
}
#endif

#endif	
