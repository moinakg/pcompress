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

#ifndef _MPLV2_LICENSE_
#	define	HELPER_DEF_BUFSIZ	(512 * 1024)
#	define	FILE_SIZE_LIMIT		(32 * 1024 * 1024)
#	define	PJG_APPVERSION1		(25)
#	define	PJG_APPVERSION2		(25)
#endif

struct scratch_buffer {
	uchar_t *in_buff;
	size_t in_bufflen;
};

#ifndef _MPLV2_LICENSE_
extern size_t packjpg_filter_process(uchar_t *in_buf, size_t len, uchar_t **out_buf);
ssize_t packjpg_filter(struct filter_info *fi, void *filter_private);

extern size_t packpnm_filter_process(uchar_t *in_buf, size_t len, uchar_t **out_buf);
ssize_t packpnm_filter(struct filter_info *fi, void *filter_private);
#endif

#ifdef _ENABLE_WAVPACK_
extern size_t wavpack_filter_encode(uchar_t *in_buf, size_t len, uchar_t **out_buf);
extern size_t wavpack_filter_decode(uchar_t *in_buf, size_t len, uchar_t **out_buf,
    ssize_t out_len);
ssize_t wavpack_filter(struct filter_info *fi, void *filter_private);
#endif

void
add_filters_by_type(struct type_data *typetab, struct filter_flags *ff)
{
	struct scratch_buffer *sdat = NULL;
	int slot;
#ifndef _MPLV2_LICENSE_

	if (ff->enable_packjpg) {
		sdat = (struct scratch_buffer *)malloc(sizeof (struct scratch_buffer));
		sdat->in_buff = NULL;
		sdat->in_bufflen = 0;

		slot = TYPE_JPEG >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = packjpg_filter;
		typetab[slot].filter_name = "packJPG";

		slot = TYPE_BMP >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = packpnm_filter;
		typetab[slot].filter_name = "packPNM";

		slot = TYPE_PNM >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = packpnm_filter;
		typetab[slot].filter_name = "packPNM";
	}
#endif

#ifdef _ENABLE_WAVPACK_
	if (ff->enable_wavpack) {
		if (!sdat) {
			sdat = (struct scratch_buffer *)malloc(sizeof (struct scratch_buffer));
			sdat->in_buff = NULL;
			sdat->in_bufflen = 0;
		}

		slot = TYPE_WAV >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = wavpack_filter;
		typetab[slot].filter_name = "WavPack";
	}
#endif
}

int
type_tag_from_filter_name(struct type_data *typetab, const char *fname, size_t len)
{
    size_t i;

    for (i = 0; i < NUM_SUB_TYPES; i++)
    {
        if (typetab[i].filter_name &&
            strncmp(fname, typetab[i].filter_name, len) == 0)
        {
            return (i << 3);
        }
    }
    return (TYPE_UNKNOWN);
}
static void
ensure_buffer(struct scratch_buffer *sdat, uint64_t len)
{
	if (sdat->in_bufflen < len) {
		if (sdat->in_buff) free(sdat->in_buff);
		sdat->in_bufflen = len;
		sdat->in_buff = malloc(sdat->in_bufflen);
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

#ifndef _MPLV2_LICENSE_
int
pjg_version_supported(char ver)
{
	return (ver >= PJG_APPVERSION1 && ver <= PJG_APPVERSION2);
}

ssize_t
packjpg_filter(struct filter_info *fi, void *filter_private)
{
	struct scratch_buffer *sdat = (struct scratch_buffer *)filter_private;
	uchar_t *mapbuf, *out;
	uint64_t len, in_size = 0, len1;
	ssize_t rv;

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > FILE_SIZE_LIMIT) // Bork on massive JPEGs
		return (FILTER_RETURN_SKIP);

	if (fi->compressing) {
		mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fi->fd, 0);
		if (mapbuf == NULL) {
			log_msg(LOG_ERR, 1, "Mmap failed in packJPG filter.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * We are trying to compress and this is not a proper jpeg. Skip.
		 */
		if (mapbuf[0] != 0xFF || mapbuf[1] != 0xD8) {
			munmap(mapbuf, len);
			return (FILTER_RETURN_SKIP);
		}
		if (strncmp((char *)&mapbuf[6], "Exif", 4) != 0 &&
		    strncmp((char *)&mapbuf[6], "JFIF", 4) != 0) {
			munmap(mapbuf, len);
			return (FILTER_RETURN_SKIP);
		}
	} else {
		/*
		 * Allocate input buffer and read archive data stream for the entry
		 * into this buffer.
		 */
		ensure_buffer(sdat, len);
		if (sdat->in_buff == NULL) {
			log_msg(LOG_ERR, 1, "Out of memory.");
			return (FILTER_RETURN_ERROR);
		}

		in_size = copy_archive_data(fi->source_arc, sdat->in_buff);
		if (in_size != len) {
			log_msg(LOG_ERR, 0, "Failed to read archive data.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * First 8 bytes in the data is the compressed size of the entry.
		 * LibArchive always zero-pads entries to their original size so
		 * we need to separately store the compressed size.
		 */
		in_size = LE64(U64_P(sdat->in_buff));
		mapbuf = sdat->in_buff + 8;

		/*
		 * We are trying to decompress and this is not a packJPG file.
		 * Write the raw data and skip. Third byte in PackJPG file is
		 * version number. We also check if it is supported.
		 */
		if (mapbuf[0] != 'J' || mapbuf[1] != 'S' || !pjg_version_supported(mapbuf[2])) {
			return (write_archive_data(fi->target_arc, sdat->in_buff,
			    len, fi->block_size));
		}
	}

	/*
	 * Compression case.
	 */
	if (fi->compressing) {
		out = NULL;
		len = packjpg_filter_process(mapbuf, len, &out);
		if (len == 0 || len >= (len1 - 8)) {
			munmap(mapbuf, len1);
			free(out);
			return (FILTER_RETURN_SKIP);
		}
		munmap(mapbuf, len1);

		in_size = LE64(len);
		rv = archive_write_data(fi->target_arc, &in_size, 8);
		if (rv != 8)
			return (rv);
		rv = archive_write_data(fi->target_arc, out, len);
		free(out);
		return (rv);
	}

	/*
	 * Decompression case.
	 */
	out = NULL;
	if ((len = packjpg_filter_process(mapbuf, in_size, &out)) == 0) {
		/*
		 * If filter failed we write out the original data and indicate a
		 * soft error to continue the archive extraction.
		 */
		free(out);
		if (write_archive_data(fi->target_arc, mapbuf, len1, fi->block_size) < len1)
			return (FILTER_RETURN_ERROR);
		return (FILTER_RETURN_SOFT_ERROR);
	}
	rv = write_archive_data(fi->target_arc, out, len, fi->block_size);
	free(out);
	return (rv);
}

ssize_t
packpnm_filter(struct filter_info *fi, void *filter_private)
{
	struct scratch_buffer *sdat = (struct scratch_buffer *)filter_private;
	uchar_t *mapbuf, *out;
	uint64_t len, in_size = 0, len1;
	ssize_t rv;

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > FILE_SIZE_LIMIT) // Bork on massive JPEGs
		return (FILTER_RETURN_SKIP);

	if (fi->compressing) {
		mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fi->fd, 0);
		if (mapbuf == NULL) {
			log_msg(LOG_ERR, 1, "Mmap failed in packPNM filter.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * We are trying to compress and this is not a proper PNM. Skip.
		 */
		if (identify_pnm_type(mapbuf, len) != 1) {
			munmap(mapbuf, len);
			return (FILTER_RETURN_SKIP);
		}
	} else {
		/*
		 * Allocate input buffer and read archive data stream for the entry
		 * into this buffer.
		 */
		ensure_buffer(sdat, len);
		if (sdat->in_buff == NULL) {
			log_msg(LOG_ERR, 1, "Out of memory.");
			return (FILTER_RETURN_ERROR);
		}

		in_size = copy_archive_data(fi->source_arc, sdat->in_buff);
		if (in_size != len) {
			log_msg(LOG_ERR, 0, "Failed to read archive data.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * First 8 bytes in the data is the compressed size of the entry.
		 * LibArchive always zero-pads entries to their original size so
		 * we need to separately store the compressed size.
		 */
		in_size = LE64(U64_P(sdat->in_buff));
		mapbuf = sdat->in_buff + 8;

		/*
		 * We are trying to decompress and this is not a packPNM file.
		 * Write the raw data and skip.
		 */
		if (identify_pnm_type(mapbuf, len - 8) != 2) {
			return (write_archive_data(fi->target_arc, sdat->in_buff,
			    len, fi->block_size));
		}
	}

	/*
	 * Compression case.
	 */
	if (fi->compressing) {
		out = NULL;
		len = packpnm_filter_process(mapbuf, len, &out);
		if (len == 0 || len >= (len1 - 8)) {
			munmap(mapbuf, len1);
			free(out);
			return (FILTER_RETURN_SKIP);
		}
		munmap(mapbuf, len1);

		in_size = LE64(len);
		rv = archive_write_data(fi->target_arc, &in_size, 8);
		if (rv != 8)
			return (rv);
		rv = archive_write_data(fi->target_arc, out, len);
		free(out);
		return (rv);
	}

	/*
	 * Decompression case.
	 */
	out = NULL;
	if ((len = packpnm_filter_process(mapbuf, in_size, &out)) == 0) {
		/*
		 * If filter failed we write out the original data and indicate a
		 * soft error to continue the archive extraction.
		 */
		free(out);
		if (write_archive_data(fi->target_arc, mapbuf, len1, fi->block_size) < len1)
			return (FILTER_RETURN_ERROR);
		return (FILTER_RETURN_SOFT_ERROR);
	}
	rv = write_archive_data(fi->target_arc, out, len, fi->block_size);
	free(out);
	return (rv);
}
#endif /* _MPLV2_LICENSE_ */

#ifdef _ENABLE_WAVPACK_
ssize_t
wavpack_filter(struct filter_info *fi, void *filter_private)
{
	struct scratch_buffer *sdat = (struct scratch_buffer *)filter_private;
	uchar_t *mapbuf, *out;
	uint64_t len, in_size = 0, len1;
	ssize_t rv;

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > FILE_SIZE_LIMIT) // Bork on massive JPEGs
		return (FILTER_RETURN_SKIP);

	if (fi->compressing) {
		mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fi->fd, 0);
		if (mapbuf == NULL) {
			log_msg(LOG_ERR, 1, "Mmap failed in WavPack filter.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * We are trying to compress and this is not a proper WAV. Skip.
		 */
		if (identify_wav_type(mapbuf, len) != 1) {
			munmap(mapbuf, len);
			return (FILTER_RETURN_SKIP);
		}
	} else {
		char *wpkstr;

		/*
		 * Allocate input buffer and read archive data stream for the entry
		 * into this buffer.
		 */
		ensure_buffer(sdat, len);
		if (sdat->in_buff == NULL) {
			log_msg(LOG_ERR, 1, "Out of memory.");
			return (FILTER_RETURN_ERROR);
		}

		in_size = copy_archive_data(fi->source_arc, sdat->in_buff);
		if (in_size != len) {
			log_msg(LOG_ERR, 0, "Failed to read archive data.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * First 8 bytes in the data is the compressed size of the entry.
		 * LibArchive always zero-pads entries to their original size so
		 * we need to separately store the compressed size.
		 */
		in_size = LE64(U64_P(sdat->in_buff));
		mapbuf = sdat->in_buff + 8;

		/*
		 * We are trying to decompress and this is not a Wavpack file.
		 * Write the raw data and skip.
		 */
		wpkstr = (char *)mapbuf;
		if (strncmp(wpkstr, "wvpk", 4) == 0) {
			return (write_archive_data(fi->target_arc, sdat->in_buff,
			    len, fi->block_size));
		}
	}

	/*
	 * Compression case.
	 */
	if (fi->compressing) {
		out = NULL;
		len = wavpack_filter_encode(mapbuf, len, &out);
		if (len == 0 || len >= (len1 - 8)) {
			munmap(mapbuf, len1);
			free(out);
			return (FILTER_RETURN_SKIP);
		}
		munmap(mapbuf, len1);

		in_size = LE64(len1);
		rv = archive_write_data(fi->target_arc, &in_size, 8);
		if (rv != 8)
			return (rv);
		rv = archive_write_data(fi->target_arc, out, len);
		free(out);
		return (rv);
	}

	/*
	 * Decompression case.
	 */
	out = NULL;
	if ((len = wavpack_filter_decode(mapbuf, len, &out, in_size)) == 0) {
		/*
		 * If filter failed we write out the original data and indicate a
		 * soft error to continue the archive extraction.
		 */
		free(out);
		if (write_archive_data(fi->target_arc, mapbuf, len1, fi->block_size) < len1)
			return (FILTER_RETURN_ERROR);
		return (FILTER_RETURN_SOFT_ERROR);
	}
	rv = write_archive_data(fi->target_arc, out, len, fi->block_size);
	free(out);
	return (rv);
}
#endif /* _ENABLE_WAVPACK_ */

