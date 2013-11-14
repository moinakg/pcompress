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

#ifndef	_PC_ARCHIVE_FILTER_H
#define	_PC_ARCHIVE_FILTER_H

#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <unistd.h>
#include <archive.h>
#include <archive_entry.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	FILTER_RETURN_SKIP	(1)
#define	FILTER_RETURN_ERROR	(-1)

struct filter_info {
	struct archive *source_arc;
	struct archive *target_arc;
	struct archive_entry *entry;
	int fd;
	int compressing, block_size;
};

struct filter_flags {
	int enable_packjpg;
};

typedef ssize_t (*filter_func_ptr)(struct filter_info *fi, void *filter_private);

struct type_data {
	void *filter_private;
	filter_func_ptr filter_func;
	char *filter_name;
};

void add_filters_by_type(struct type_data *typetab, struct filter_flags *ff);

#ifdef	__cplusplus
}
#endif

#endif	
