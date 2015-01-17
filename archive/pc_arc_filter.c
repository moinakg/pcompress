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
#include "pc_arc_filter.h"
#include "pc_archive.h"

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
extern size_t wavpack_filter_encode(uchar_t *in_buf, size_t len, uchar_t **out_buf,
    int cmp_level);
extern size_t wavpack_filter_decode(uchar_t *in_buf, size_t len, uchar_t **out_buf,
    ssize_t out_len);
ssize_t wavpack_filter(struct filter_info *fi, void *filter_private);
#endif

size_t dispack_filter_encode(uchar_t *inData, size_t len, uchar_t **out_buf);
size_t dispack_filter_decode(uchar_t *inData, size_t len, uchar_t **out_buf);
ssize_t dispack_filter(struct filter_info *fi, void *filter_private);

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
		typetab[slot].result_type = -1;

		slot = TYPE_BMP >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = packpnm_filter;
		typetab[slot].filter_name = "packPNM";
		typetab[slot].result_type = TYPE_BINARY;

		slot = TYPE_PNM >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = packpnm_filter;
		typetab[slot].filter_name = "packPNM";
		typetab[slot].result_type = TYPE_BINARY;
	}
#endif

	if (ff->exe_preprocess) {
		if (!sdat) {
			sdat = (struct scratch_buffer *)malloc(sizeof (struct scratch_buffer));
			sdat->in_buff = NULL;
			sdat->in_bufflen = 0;
		}
		slot = TYPE_EXE32_PE >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = dispack_filter;
		typetab[slot].filter_name = "Dispack";
		typetab[slot].result_type = 0;
	}

#ifdef _ENABLE_WAVPACK_
	if (ff->enable_wavpack) {
		if (!sdat) {
			sdat = (struct scratch_buffer *)malloc(sizeof (struct scratch_buffer));
			sdat->in_buff = NULL;
			sdat->in_bufflen = -1;
		}

		slot = TYPE_WAV >> 3;
		typetab[slot].filter_private = sdat;
		typetab[slot].filter_func = wavpack_filter;
		typetab[slot].filter_name = "WavPack";
		typetab[slot].result_type = -1;
	}
#endif
}

int
type_tag_from_filter_name(struct type_data *typetab, const char *fname, size_t len)
{
    size_t i;

    for (i = 0; i <= NUM_SUB_TYPES; i++)
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

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > PJG_FILE_SIZE_LIMIT) // Bork on massive JPEGs
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
			uint8_t *out = malloc(len);

			memcpy(out, sdat->in_buff, len);
			fi->fout->output_type = FILTER_OUTPUT_MEM;
			fi->fout->out = out;
			fi->fout->out_size = len;
			return (FILTER_RETURN_SOFT_ERROR);
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

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		fi->fout->hdr.in_size = LE64(len);
		return (ARCHIVE_OK);
	}

	/*
	 * Decompression case.
	 */
	out = NULL;
	if ((len = packjpg_filter_process(mapbuf, in_size, &out)) == 0) {
		/*
		 * If filter failed we indicate a soft error to continue the
		 * archive extraction.
		 */
		free(out);
		out = malloc(len);
		memcpy(out, sdat->in_buff, len);

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		return (FILTER_RETURN_SOFT_ERROR);
	}

	fi->fout->output_type = FILTER_OUTPUT_MEM;
	fi->fout->out = out;
	fi->fout->out_size = len;
	return (ARCHIVE_OK);
}

ssize_t
packpnm_filter(struct filter_info *fi, void *filter_private)
{
	struct scratch_buffer *sdat = (struct scratch_buffer *)filter_private;
	uchar_t *mapbuf, *out;
	uint64_t len, in_size = 0, len1;

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > PJG_FILE_SIZE_LIMIT) // Bork on massive JPEGs
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
			uint8_t *out = malloc(len);

			memcpy(out, sdat->in_buff, len);
			fi->fout->output_type = FILTER_OUTPUT_MEM;
			fi->fout->out = out;
			fi->fout->out_size = len;
			return (FILTER_RETURN_SOFT_ERROR);
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

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		fi->fout->hdr.in_size = LE64(len);
		return (ARCHIVE_OK);
	}

	/*
	 * Decompression case.
	 */
	out = NULL;
	if ((len = packpnm_filter_process(mapbuf, in_size, &out)) == 0) {
		/*
		 * If filter failed we indicate a soft error to continue the
		 * archive extraction.
		 */
		free(out);
		out = malloc(len);
		memcpy(out, sdat->in_buff, len);

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		return (FILTER_RETURN_SOFT_ERROR);
	}

	fi->fout->output_type = FILTER_OUTPUT_MEM;
	fi->fout->out = out;
	fi->fout->out_size = len;
	return (ARCHIVE_OK);
}
#endif /* _MPLV2_LICENSE_ */

#ifdef _ENABLE_WAVPACK_
ssize_t
wavpack_filter(struct filter_info *fi, void *filter_private)
{
	struct scratch_buffer *sdat = (struct scratch_buffer *)filter_private;
	uchar_t *mapbuf, *out;
	uint64_t len, in_size = 0, len1;

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > WVPK_FILE_SIZE_LIMIT)
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
		if (strncmp(wpkstr, "wvpk", 4) != 0) {
			uint8_t *out = malloc(len);

			memcpy(out, sdat->in_buff, len);
			fi->fout->output_type = FILTER_OUTPUT_MEM;
			fi->fout->out = out;
			fi->fout->out_size = len;
			return (FILTER_RETURN_SOFT_ERROR);
		}
	}

	/*
	 * Compression case.
	 */
	if (fi->compressing) {
		out = NULL;
		len = wavpack_filter_encode(mapbuf, len, &out, fi->cmp_level);
		if (len == 0 || len >= (len1 - 8)) {
			munmap(mapbuf, len1);
			free(out);
			return (FILTER_RETURN_SKIP);
		}
		munmap(mapbuf, len1);

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		fi->fout->hdr.in_size = LE64(len1);
		return (ARCHIVE_OK);
	}

	/*
	 * Decompression case.
	 */
	out = NULL;
	if ((len = wavpack_filter_decode(mapbuf, in_size, &out, len)) == 0) {
		/*
		 * If filter failed we indicate a soft error to continue the
		 * archive extraction.
		 */
		free(out);
		out = malloc(len);
		memcpy(out, sdat->in_buff, len);

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		return (FILTER_RETURN_SOFT_ERROR);
	}

	fi->fout->output_type = FILTER_OUTPUT_MEM;
	fi->fout->out = out;
	fi->fout->out_size = len;
	return (ARCHIVE_OK);
}
#endif /* _ENABLE_WAVPACK_ */

ssize_t
dispack_filter(struct filter_info *fi, void *filter_private)
{
	struct scratch_buffer *sdat = (struct scratch_buffer *)filter_private;
	uchar_t *mapbuf, *out;
	uint64_t len, in_size = 0, len1;

	len = archive_entry_size(fi->entry);
	len1 = len;
	if (len > WVPK_FILE_SIZE_LIMIT) // Bork on massive files
		return (FILTER_RETURN_SKIP);

	if (fi->compressing) {
		mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fi->fd, 0);
		if (mapbuf == NULL) {
			log_msg(LOG_ERR, 1, "Mmap failed in Dispack filter.");
			return (FILTER_RETURN_ERROR);
		}

		/*
		 * No check for supported 32-bit exe here. EXE types are always
		 * detected by file header analysis. So no need to duplicate here.
		 */
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
		mapbuf = sdat->in_buff;

		/*
		 * No check for supported EXE types needed here since supported
		 * and filtered files are tagged in the archive using xattrs during
		 * compression.
		 */
	}

	/*
	 * Compression case.
	 */
	if (fi->compressing) {
		out = NULL;
		len = dispack_filter_encode(mapbuf, len, &out);
		if (len == 0) {
			munmap(mapbuf, len1);
			free(out);
			return (FILTER_RETURN_SKIP);
		}
		munmap(mapbuf, len1);

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		fi->fout->hdr_valid = 0;
		return (ARCHIVE_OK);
	}

	/*
	 * Decompression case.
	 */
	out = NULL;
	if ((len = dispack_filter_decode(mapbuf, in_size, &out)) == 0) {
		/*
		 * If filter failed we indicate a soft error to continue the
		 * archive extraction.
		 */
		free(out);
		out = malloc(len);
		memcpy(out, sdat->in_buff, len);

		fi->fout->output_type = FILTER_OUTPUT_MEM;
		fi->fout->out = out;
		fi->fout->out_size = len;
		return (FILTER_RETURN_SOFT_ERROR);
	}

	fi->fout->output_type = FILTER_OUTPUT_MEM;
	fi->fout->out = out;
	fi->fout->out_size = len;
	return (ARCHIVE_OK);
}

