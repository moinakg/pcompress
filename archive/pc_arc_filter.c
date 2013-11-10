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

/*
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <utils.h>
#include <sys/mman.h>
#include <ctype.h>
#include <archive.h>
#include <archive_entry.h>
#include "pc_arc_filter.h"
#include "pc_archive.h"

#define	PACKJPG_DEF_BUFSIZ	(512 * 1024)
#define	JPG_SIZE_LIMIT		(100 * 1024 * 1024)

struct packjpg_filter_data {
	uchar_t *buff;
	size_t bufflen;
};

extern size_t packjpg_filter_process(uchar_t *in_buf, size_t len, uchar_t **out_buf);

int packjpg_filter(struct filter_info *fi, void *filter_private);

void
add_filters_by_ext()
{
	struct packjpg_filter_data *pjdat;

	pjdat = (struct packjpg_filter_data *)malloc(sizeof (struct packjpg_filter_data));
	pjdat->buff = (uchar_t *)malloc(PACKJPG_DEF_BUFSIZ);
	pjdat->bufflen = PACKJPG_DEF_BUFSIZ;
	if (insert_filter_data(packjpg_filter, pjdat, "pjg") != 0) {
		free(pjdat->buff);
		free(pjdat);
		log_msg(LOG_WARN, 0, "Failed to add filter module for packJPG.");
	}
}

/* a short reminder about input/output stream types
   for the pjglib_init_streams() function
	
	if input is file
	----------------
	in_scr -> name of input file
	in_type -> 0
	in_size -> ignore
	
	if input is memory
	------------------
	in_scr -> array containg data
	in_type -> 1
	in_size -> size of data array
	
	if input is *FILE (f.e. stdin)
	------------------------------
	in_src -> stream pointer
	in_type -> 2
	in_size -> ignore
	
	vice versa for output streams! */

int
packjpg_filter(struct filter_info *fi, void *filter_private)
{
	struct packjpg_filter_data *pjdat = (struct packjpg_filter_data *)filter_private;
	uchar_t *mapbuf, *out;
	size_t len;

	len = archive_entry_size(fi->entry);
	if (len > JPG_SIZE_LIMIT) // Bork on massive JPEGs
		return (-1);

	mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fi->fd, 0);
	if (mapbuf == NULL)
		return (-1);

	if (pjdat->bufflen < len) {
		free(pjdat->buff);
		pjdat->bufflen = len;
		pjdat->buff = malloc(pjdat->bufflen);
		if (pjdat->buff == NULL) {
			log_msg(LOG_ERR, 1, "Out of memory.");
			munmap(mapbuf, len);
			return (-1);
		}
	}

	/*
	 * Helper routine to bridge to packJPG C++ lib, without changing packJPG itself.
	 */
	out = pjdat->buff;
	if ((len = packjpg_filter_process(mapbuf, len, &out)) == 0) {
		return (-1);
	}
	return (archive_write_data(fi->target_arc, out, len));
}

