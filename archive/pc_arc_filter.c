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
#define	JPG_SIZE_LIMIT		(25 * 1024 * 1024)

struct packjpg_filter_data {
	uchar_t *buff, *in_buff;
	size_t bufflen, in_bufflen;
};

extern size_t packjpg_filter_process(uchar_t *in_buf, size_t len, uchar_t **out_buf);

int64_t packjpg_filter(struct filter_info *fi, void *filter_private);

void
add_filters_by_type(struct type_data *typetab, struct filter_flags *ff)
{
	struct packjpg_filter_data *pjdat;
	int slot;

	if (ff->enable_packjpg) {
		pjdat = (struct packjpg_filter_data *)malloc(sizeof (struct packjpg_filter_data));
		pjdat->buff = (uchar_t *)malloc(PACKJPG_DEF_BUFSIZ);
		pjdat->bufflen = PACKJPG_DEF_BUFSIZ;
		pjdat->in_buff = NULL;
		pjdat->in_bufflen = 0;

		slot = TYPE_JPEG >> 3;
		typetab[slot].filter_private = pjdat;
		typetab[slot].filter_func = packjpg_filter;
		typetab[slot].filter_name = "packJPG";
	}
}

/*
 * Copy current entry data from the archive being extracted into the given buffer.
 */
static ssize_t
copy_archive_data(struct archive *ar, uchar_t *out_buf)
{
	int64_t offset;
	const void *buff;
	size_t size, tot;
	int r;

	tot = 0;
	for (;;) {
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK)
			return (0);
		memcpy(out_buf + offset, buff, size);
		tot += size;
	}
	return (tot);
}

/*
 * Copy the given buffer into the archive stream.
 */
static ssize_t
write_archive_data(struct archive *aw, uchar_t *out_buf, size_t len, int block_size)
{
	int64_t offset;
	uchar_t *buff;
	int r;
	size_t tot;

	buff = out_buf;
	offset = 0;
	tot = len;
	while (len > 0) {
		if (len < block_size)
			block_size = len;
		r = (int)archive_write_data_block(aw, buff, block_size, offset);
		if (r < ARCHIVE_WARN)
			r = ARCHIVE_WARN;
		if (r != ARCHIVE_OK) {
			return (r);
		}
		offset += block_size;
		len -= block_size;
		buff += block_size;
	}
	return (tot);
}

/*
 * Helper routine to bridge to packJPG C++ lib, without changing packJPG itself.
 */
ssize_t
packjpg_filter(struct filter_info *fi, void *filter_private)
{
	struct packjpg_filter_data *pjdat = (struct packjpg_filter_data *)filter_private;
	uchar_t *mapbuf, *out;
	uint64_t len, in_size = 0, len1;

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > JPG_SIZE_LIMIT) // Bork on massive JPEGs
		return (FILTER_RETURN_SKIP);

	if (fi->compressing) {
		mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fi->fd, 0);
		if (mapbuf == NULL) {
			log_msg(LOG_ERR, 1, "Mmap failed in packJPG filter.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * We are trying to compress and this is not a jpeg. Skip.
		 */
		if (mapbuf[0] != 0xFF && mapbuf[1] != 0xD8) {
			munmap(mapbuf, len);
			return (FILTER_RETURN_SKIP);
		}
	} else {
		/*
		 * Allocate input buffer and read archive data stream for the entry
		 * into this buffer.
		 */
		if (pjdat->in_bufflen < len) {
			if (pjdat->in_buff) free(pjdat->in_buff);
			pjdat->in_bufflen = len;
			pjdat->in_buff = malloc(pjdat->in_bufflen);
			if (pjdat->in_buff == NULL) {
				log_msg(LOG_ERR, 1, "Out of memory.");
				return (FILTER_RETURN_ERROR);
			}
		}

		in_size = copy_archive_data(fi->source_arc, pjdat->in_buff);
		if (in_size != len) {
			log_msg(LOG_ERR, 0, "Failed to read archive data.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * First 8 bytes in the data is the compressed size of the entry.
		 * LibArchive always zero-pads entries to their original size so
		 * we need to separately store the compressed size.
		 */
		in_size = U64_P(pjdat->in_buff);
		mapbuf = pjdat->in_buff + 8;

		/*
		 * We are trying to decompress and this is not a packJPG file, or is
		 * a normal Jpeg.
		 * Write the raw data and skip.
		 */
		if ((mapbuf[0] != 'J' && mapbuf[1] != 'S') ||
		    (pjdat->in_buff[0] == 0xFF && pjdat->in_buff[1] == 0xD8)) {
			return (write_archive_data(fi->target_arc, pjdat->in_buff,
			    in_size, fi->block_size));
		}
	}
	if (pjdat->bufflen < len) {
		free(pjdat->buff);
		pjdat->bufflen = len;
		pjdat->buff = malloc(pjdat->bufflen);
		if (pjdat->buff == NULL) {
			log_msg(LOG_ERR, 1, "Out of memory.");
			munmap(mapbuf, len);
			return (FILTER_RETURN_ERROR);
		}
	}

	/*
	 * Compression case.
	 */
	if (fi->compressing) {
		ssize_t rv;

		out = pjdat->buff;
		if ((len = packjpg_filter_process(mapbuf, len, &out)) == 0) {
			munmap(mapbuf, len1);
			return (FILTER_RETURN_SKIP);
		}
		munmap(mapbuf, len1);

		in_size = LE64(len);
		rv = archive_write_data(fi->target_arc, &in_size, 8);
		if (rv != 8)
			return (rv);
		return (archive_write_data(fi->target_arc, out, len));
	}

	/*
	 * Decompression case.
	 */
	out = pjdat->buff;
	if ((len = packjpg_filter_process(mapbuf, in_size, &out)) == 0) {
		/*
		 * If filter failed we write out the original data and indicate skip
		 * to continue the archive extraction.
		 */
		if (write_archive_data(fi->target_arc, mapbuf, len1, fi->block_size) < len1)
			return (FILTER_RETURN_ERROR);
		return (FILTER_RETURN_SKIP);
	}
	return (write_archive_data(fi->target_arc, out, len, fi->block_size));
}

