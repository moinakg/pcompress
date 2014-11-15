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
 * This file includes all the archiving related functions. Pathnames are sorted
 * based on extension (or first 4 chars of name if no extension) and size. A simple
 * external merge sort is used. This sorting yields better compression ratio.
 *
 * Sorting is enabled for compression levels greater than 6.
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
#include <pthread.h>
#include <sys/mman.h>
#include <ctype.h>
#include <archive.h>
#include <archive_entry.h>
#include <phash/phash.h>
#include <phash/extensions.h>
#include <phash/standard.h>
#include "archive/pc_archive.h"
#include "meta_stream.h"

#undef _FEATURES_H
#define _XOPEN_SOURCE 700
#include <ftw.h>
#include <stdint.h>

static int inited = 0, filters_inited = 0;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct ext_hash_entry {
	uint64_t extnum;
	int type;
} *exthtab = NULL;

static struct type_data typetab[NUM_SUB_TYPES];

/*
AE_IFREG   Regular file
AE_IFLNK   Symbolic link
AE_IFSOCK  Socket
AE_IFCHR   Character device
AE_IFBLK   Block device
AE_IFDIR   Directory
AE_IFIFO   Named pipe (fifo)
*/

#define	ARC_ENTRY_OVRHEAD	1024
#define	MMAP_SIZE		(1024 * 1024)
#define	SORT_BUF_SIZE		(65536)
#define	NAMELEN			4
#define	TEMP_MMAP_SIZE		(128 * 1024)
#define	AW_BLOCK_SIZE		(256 * 1024)

typedef struct member_entry {
	uchar_t name[NAMELEN];
	uint32_t file_pos; // 32-bit file position to limit memory usage.
	uint64_t size;
} member_entry_t;

struct sort_buf {
	member_entry_t members[SORT_BUF_SIZE]; // Use 1MB per sorted buffer
	int pos, max;
	struct sort_buf *next;
};

static struct arc_list_state {
	uchar_t *pbuf;
	uint64_t bufsiz, bufpos, arc_size, pathlist_size;
	uint32_t fcount;
	int fd;
	struct sort_buf *srt, *head;
	int srt_pos;
} a_state;

pthread_mutex_t nftw_mutex = PTHREAD_MUTEX_INITIALIZER;

static int detect_type_by_ext(const char *path, int pathlen);
static int detect_type_from_ext(const char *ext, int len);
static int detect_type_by_data(uchar_t *buf, size_t len);

/*
 * Archive writer callback routines for archive creation operation.
 */
static int
arc_open_callback(struct archive *arc, void *ctx)
{
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;

	Sem_Init(&(pctx->read_sem), 0, 0);
	Sem_Init(&(pctx->write_sem), 0, 0);
	pctx->arc_buf = NULL;
	pctx->arc_buf_pos = 0;
	pctx->arc_buf_size = 0;
	return (ARCHIVE_OK);
}

static int
creat_close_callback(struct archive *arc, void *ctx)
{
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;

	pctx->arc_closed = 1;
	if (pctx->arc_buf) {
		Sem_Post(&(pctx->read_sem));
	} else {
		pctx->arc_buf_pos = 0;
	}
	return (ARCHIVE_OK);
}

static ssize_t
creat_write_callback(struct archive *arc, void *ctx, const void *buf, size_t len)
{
	uchar_t *buff = (uchar_t *)buf;
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;
	size_t remaining;

	if (pctx->arc_closed) {
		archive_set_error(arc, ARCHIVE_EOF, "End of file when writing archive.");
		return (-1);
	}

	if (archive_request_is_metadata(arc) && pctx->meta_stream) {
		int rv;

		/*
		 * Send the buf pointer over to the metadata thread.
		 */
		rv = meta_ctx_send(pctx->meta_ctx, &buf, &len);
		if (rv == 0) {
			archive_set_error(arc, ARCHIVE_EOF, "Metadata Thread communication error.");
			return (-1);

		} else if (rv == -1) {
			archive_set_error(arc, ARCHIVE_EOF, "Error reported by Metadata Thread.");
			return (-1);
		}
		return (len);
	}

	if (!pctx->arc_writing) {
		Sem_Wait(&(pctx->write_sem));
	}

	if (pctx->arc_buf == NULL || pctx->arc_buf_size == 0) {
		archive_set_error(arc, ARCHIVE_EOF, "End of file when writing archive.");
		return (-1);
	}
	pctx->arc_writing = 1;

	remaining = len;
	while (remaining && !pctx->arc_closed) {
		uchar_t *tbuf;

		tbuf = pctx->arc_buf + pctx->arc_buf_pos;

		/*
		 * Determine if we should return the accumulated data to the caller.
		 * This is done if the data type changes and at least some minimum amount
		 * of data has accumulated in the buffer.
		 */
		if (pctx->btype != pctx->ctype) {
			if (pctx->btype == TYPE_UNKNOWN || pctx->arc_buf_pos == 0) {
				pctx->btype = pctx->ctype;
				if (pctx->arc_buf_pos != 0)
					pctx->interesting = 1;
			} else {
				if (pctx->arc_buf_pos < pctx->min_chunk) {
					int diff = pctx->min_chunk - (int)(pctx->arc_buf_pos);
					if (len >= diff)
						pctx->btype = pctx->ctype;
					else
						pctx->ctype = pctx->btype;
					pctx->interesting = 1;
				} else {
					pctx->arc_writing = 0;
					Sem_Post(&(pctx->read_sem));
					Sem_Wait(&(pctx->write_sem));
					tbuf = pctx->arc_buf + pctx->arc_buf_pos;
					pctx->arc_writing = 1;
					pctx->btype = pctx->ctype;
				}
			}
		}

		if (remaining > pctx->arc_buf_size - pctx->arc_buf_pos) {
			size_t nlen = pctx->arc_buf_size - pctx->arc_buf_pos;
			memcpy(tbuf, buff, nlen);
			remaining -= nlen;
			pctx->arc_buf_pos += nlen;
			buff += nlen;
			pctx->arc_writing = 0;
			Sem_Post(&(pctx->read_sem));
			Sem_Wait(&(pctx->write_sem));
			pctx->arc_writing = 1;
		} else {
			memcpy(tbuf, buff, remaining);
			pctx->arc_buf_pos += remaining;
			remaining = 0;
			if (pctx->arc_buf_pos == pctx->arc_buf_size) {
				pctx->arc_writing = 0;
				Sem_Post(&(pctx->read_sem));
			}
			break;
		}
	}

	return (len - remaining);
}

int64_t
archiver_read(void *ctx, void *buf, uint64_t count)
{
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;

	if (pctx->arc_closed)
		return (0);

	if (pctx->arc_buf != NULL) {
		log_msg(LOG_ERR, 0, "Incorrect sequencing of archiver_read() call.");
		return (-1);
	}

	pctx->arc_buf = buf;
	pctx->arc_buf_size = count;
	pctx->arc_buf_pos = 0;
	pctx->btype = TYPE_UNKNOWN;
	Sem_Post(&(pctx->write_sem));
	Sem_Wait(&(pctx->read_sem));
	pctx->arc_buf = NULL;
	return (pctx->arc_buf_pos);
}

int
archiver_close(void *ctx)
{
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;

	pctx->arc_closed = 1;
	pctx->arc_buf = NULL;
	pctx->arc_buf_size = 0;
	Sem_Post(&(pctx->write_sem));
	Sem_Post(&(pctx->read_sem));
	return (0);
}

static int
extract_close_callback(struct archive *arc, void *ctx)
{
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;

	pctx->arc_closed = 1;
	if (pctx->arc_buf) {
		Sem_Post(&(pctx->write_sem));
	} else {
		pctx->arc_buf_size = 0;
	}
	return (ARCHIVE_OK);
}

static ssize_t
extract_read_callback(struct archive *arc, void *ctx, const void **buf)
{
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;

	if (pctx->arc_closed) {
		pctx->arc_buf_size = 0;
		log_msg(LOG_WARN, 0, "End of file.");
		archive_set_error(arc, ARCHIVE_EOF, "End of file.");
		return (-1);
	}

	if (archive_request_is_metadata(arc) && pctx->meta_stream) {
		int rv;
		size_t len;

		/*
		 * Send the buf pointer over to the metadata thread.
		 */
		len = 0;
		rv = meta_ctx_send(pctx->meta_ctx, buf, &len);
		if (rv == 0) {
			archive_set_error(arc, ARCHIVE_EOF, "Metadata Thread communication error.");
			return (-1);
			
		} else if (rv == -1) {
			archive_set_error(arc, ARCHIVE_EOF, "Error reported by Metadata Thread.");
			return (-1);
		}
		return (len);
	}

	/*
	 * When listing TOC we just return dummy data to be thrown away.
	 */
	if (pctx->list_mode && pctx->meta_stream) {
		*buf = pctx->temp_mmap_buf;
		return (pctx->temp_mmap_len);
	}

	if (!pctx->arc_writing) {
		Sem_Wait(&(pctx->read_sem));
	} else {
		Sem_Post(&(pctx->write_sem));
		Sem_Wait(&(pctx->read_sem));
	}

	if (pctx->arc_buf == NULL || pctx->arc_buf_size == 0) {
		pctx->arc_buf_size = 0;
		log_msg(LOG_ERR, 0, "End of file when extracting archive.");
		archive_set_error(arc, ARCHIVE_EOF, "End of file when extracting archive.");
		return (-1);
	}

	pctx->arc_writing = 1;
	*buf = pctx->arc_buf;

	return (pctx->arc_buf_size);
}

int64_t
archiver_write(void *ctx, void *buf, uint64_t count)
{
	pc_ctx_t *pctx = (pc_ctx_t *)ctx;

	if (pctx->arc_closed) {
		log_msg(LOG_WARN, 0, "Archive extractor closed unexpectedly");
		return (0);
	}

	if (pctx->arc_buf != NULL) {
		log_msg(LOG_ERR, 0, "Incorrect sequencing of archiver_read() call.");
		return (-1);
	}

	pctx->arc_buf = buf;
	pctx->arc_buf_size = count;
	Sem_Post(&(pctx->read_sem));
	Sem_Wait(&(pctx->write_sem));
	pctx->arc_buf = NULL;
	return (pctx->arc_buf_size);
}

/*
 * Comparison function for sorting pathname members. Sort by name/extension and then
 * by size.
 */
static int
compare_members(const void *a, const void *b) {
	int rv, i;
	member_entry_t *mem1 = (member_entry_t *)a;
	member_entry_t *mem2 = (member_entry_t *)b;
	uint64_t sz1, sz2;

	/*
	 * First compare MSB of size. That separates extension and non-extension
	 * files.
	 */
	sz1 = mem1->size & 0x8000000000000000;
	sz2 = mem2->size & 0x8000000000000000;
	if (sz1 > sz2)
		return (1);
	else if (sz1 < sz2)
		return (-1);

	rv = 0;
	for (i = 0; i < NAMELEN; i++) {
		rv = mem1->name[i] - mem2->name[i];
		if (rv != 0)
			return (rv);
	}

	/*
	 * Clear high bits of size. They are just flags.
	 */
	sz1 = mem1->size & 0x7FFFFFFFFFFFFFFF;
	sz2 = mem2->size & 0x7FFFFFFFFFFFFFFF;
	if (sz1 > sz2)
		return (1);
	else if (sz1 < sz2)
		return (-1);
	return (0);
}

/*
 * Tell if path entry mem1 is "less than" path entry mem2. This function
 * is used during the merge phase.
 */
static int
compare_members_lt(member_entry_t *mem1, member_entry_t *mem2) {
	int rv, i;
	uint64_t sz1, sz2;

	/*
	 * First compare MSB of size. That separates extension and non-extension
	 * files.
	 */
	sz1 = mem1->size & 0x8000000000000000;
	sz2 = mem2->size & 0x8000000000000000;
	if (sz1 < sz2)
		return (1);

	rv = 0;
	for (i = 0; i < NAMELEN; i++) {
		rv = mem1->name[i] - mem2->name[i];
		if (rv < 0)
			return (1);
		else if (rv > 0)
			return (0);
	}

	/*
	 * Clear high bits of size. They are just flags.
	 */
	sz1 = mem1->size & 0x7FFFFFFFFFFFFFFF;
	sz2 = mem2->size & 0x7FFFFFFFFFFFFFFF;
	if (sz1 < sz2)
		return (1);
	return (0);
}

/*
 * Fetch the next entry from the pathlist file. If we are doing sorting then this
 * fetches the next entry in ascending order of the predetermined sort keys.
 */
static int
read_next_path(pc_ctx_t *pctx, char *fpath, char **namechars, int *fpathlen)
{
	short namelen;
	ssize_t rbytes;
	uchar_t *buf;
	int n;

	if (pctx->enable_archive_sort) {
		member_entry_t *mem1, *mem2;
		struct sort_buf *srt, *srt1, *psrt, *psrt1;

		/*
		 * Here we have a set of sorted buffers and we do the external merge phase where
		 * we pop the buffer entry that is smallest.
		 */
		srt = (struct sort_buf *)pctx->archive_sort_buf;
		if (!srt) return (0);
		srt1 = srt;
		psrt = srt;
		psrt1 = psrt;
		mem1 = &(srt->members[srt->pos]);
		srt = srt->next;
		while (srt) {
			mem2 = &(srt->members[srt->pos]);
			if (compare_members_lt(mem2, mem1)) {
				mem1 = mem2;
				srt1 = srt;
				psrt1 = psrt;
			}
			psrt =  srt;
			srt = srt->next;
		}

		/*
		 * If we are not using mmap then seek to the position of the current entry, otherwise
		 * just note the entry position.
		 */
		if (pctx->temp_mmap_len == 0) {
			if (lseek(pctx->archive_members_fd, mem1->file_pos, SEEK_SET) == (off_t)-1) {
				log_msg(LOG_ERR, 1, "Error seeking in archive members file.");
				return (-1);
			}
		} else {
			pctx->temp_file_pos = mem1->file_pos;
		}

		/*
		 * Increment popped position of the current buffer and check if it is empty.
		 * The empty buffer is freed and is taken out of the linked list of buffers.
		 */
		srt1->pos++;
		if (srt1->pos > srt1->max) {
			if (srt1 == pctx->archive_sort_buf) {
				pctx->archive_sort_buf = srt1->next;
				free(srt1);
			} else {
				psrt1->next = srt1->next;
				free(srt1);
			}
		}
	}

	/*
	 * Mmap handling. If requested entry is in current mmap region read it. Otherwise attempt
	 * new mmap.
	 */
	if (pctx->temp_mmap_len > 0) {
		int retried;

		if (pctx->temp_file_pos < pctx->temp_mmap_pos ||
		    pctx->temp_file_pos - pctx->temp_mmap_pos > pctx->temp_mmap_len ||
		    pctx->temp_mmap_len - (pctx->temp_file_pos - pctx->temp_mmap_pos) < 3) {
			uint32_t adj;

do_mmap:
			munmap(pctx->temp_mmap_buf, pctx->temp_mmap_len);
			adj = pctx->temp_file_pos % pctx->pagesize;
			pctx->temp_mmap_pos = pctx->temp_file_pos - adj;
			pctx->temp_mmap_len = pctx->archive_temp_size - pctx->temp_mmap_pos;

			if (pctx->temp_mmap_len > TEMP_MMAP_SIZE)
				pctx->temp_mmap_len = TEMP_MMAP_SIZE ;
			pctx->temp_mmap_buf = mmap(NULL, pctx->temp_mmap_len, PROT_READ,
			    MAP_SHARED, pctx->archive_members_fd, pctx->temp_mmap_pos);
			if (pctx->temp_mmap_buf == NULL) {
				log_msg(LOG_ERR, 1, "Error mmap-ing archive members file.");
				return (-1);
			}
		}

		retried = 0;
		buf = pctx->temp_mmap_buf + (pctx->temp_file_pos - pctx->temp_mmap_pos);
		namelen = U32_P(buf);
		pctx->temp_file_pos += 2;

		/*
		 * If length of pathname entry exceeds current mmap region, repeat mmap
		 * at the entry offset. Only one repeat attempt is made. If there is a
		 * failure then we give up.
		 */
		if (pctx->temp_mmap_len - (pctx->temp_file_pos - pctx->temp_mmap_pos) < namelen) {
			if (!retried) {
				pctx->temp_file_pos -= 2;
				retried = 1;
				goto do_mmap;
			} else {
				log_msg(LOG_ERR, 0, "Unable to mmap after retry.");
				return (-1);
			}
		}

		buf = pctx->temp_mmap_buf + (pctx->temp_file_pos - pctx->temp_mmap_pos);
		memcpy(fpath, buf, namelen);
		fpath[namelen] = '\0';
		*fpathlen = namelen;

		n = namelen-1;
		while (fpath[n] == '/' && n > 0) n--;
		while (fpath[n] != '/' && fpath[n] != '\\' && n > 0) n--;
		*namechars = &fpath[n+1];

		pctx->temp_file_pos += namelen;
		return (namelen);
	}

	/*
	 * This code is used if mmap is not being used for the pathlist file.
	 */
	if ((rbytes = Read(pctx->archive_members_fd, &namelen, sizeof(namelen))) != 0) {
		if (rbytes < 2) {
			log_msg(LOG_ERR, 1, "Error reading archive members file.");
			return (-1);
		}
		rbytes = Read(pctx->archive_members_fd, fpath, namelen);
		if (rbytes < namelen) {
			log_msg(LOG_ERR, 1, "Error reading archive members file.");
			return (-1);
		}
		fpath[namelen] = '\0';
		*fpathlen = namelen;

		n = namelen-1;
		while (fpath[n] == '/' && n > 0) n--;
		while (fpath[n] != '/' && fpath[n] != '\\' && n > 0) n--;
		*namechars = &fpath[n+1];
	}
	return (rbytes);
}

/*
 * Build list of pathnames in a temp file.
 */
static int
add_pathname(const char *fpath, const struct stat *sb,
                    int tflag, struct FTW *ftwbuf)
{
	short len;
	uchar_t *buf;
	const char *basename;

	if (tflag == FTW_DNR || tflag == FTW_NS) {
		log_msg(LOG_WARN, 0, "Cannot access %s\n", fpath);
		return (0);
	}

	/*
	 * Pathname entries are pushed into a memory buffer till buffer is full. The
	 * buffer is then flushed to disk. This is for decent performance.
	 */
	a_state.arc_size += (sb->st_size + ARC_ENTRY_OVRHEAD);
	len = strlen(fpath);
	if (a_state.bufpos + len + 14 > a_state.bufsiz) {
		ssize_t wrtn = Write(a_state.fd, a_state.pbuf, a_state.bufpos);
		if (wrtn < a_state.bufpos) {
			log_msg(LOG_ERR, 1, "Write: ");
			return (-1);
		}
		a_state.bufpos = 0;
		a_state.pathlist_size += wrtn;
	}

	/*
	 * If we are sorting path entries then sort per buffer and then merge when iterating
	 * through all the path entries.
	 */
	if (a_state.srt) {
		member_entry_t *member;
		int i;
		char *dot;

		/*
		 * Paranoid check (Well, we can have a sparse file of any size ...).
		 * When sorting pathnames, we can't handle files close to INT64_MAX size.
		 */
		if (sb->st_size > INT64_MAX - 255) {
			log_msg(LOG_ERR, 0, "%s:\nCannot handle files > %lld bytes when sorting!",
			    fpath, INT64_MAX - 255);
		}
		basename = &fpath[ftwbuf->base];
		if (a_state.srt_pos == SORT_BUF_SIZE) {
			struct sort_buf *srt;

			/*
			 * Sort Buffer is full so sort it. Sorting is done by file extension and size.
			 * If file has no extension then first 4 chars of the filename are used.
			 */
			srt = (struct sort_buf *)malloc(sizeof (struct sort_buf));
			if (srt == NULL) {
				log_msg(LOG_WARN, 0, "Out of memory for sort buffer. Continuing without sorting.");
				a_state.srt = a_state.head;
				while (a_state.srt) {
					struct sort_buf *srt;
					srt = a_state.srt->next;
					free(a_state.srt);
					a_state.srt = srt;
					goto cont;
				}
			} else {
				log_msg(LOG_INFO, 0, "Sorting ...");
				a_state.srt->max = a_state.srt_pos - 1;
				qsort(a_state.srt->members, SORT_BUF_SIZE, sizeof (member_entry_t), compare_members);
				srt->next = NULL;
				srt->pos = 0;
				a_state.srt->next = srt;
				a_state.srt = srt;
				a_state.srt_pos = 0;
			}
		}

		/*
		 * The total size of path list file that can be handled when sorting is 4GB to
		 * limit memory usage. If total accumulated path entries exceed 4GB in bytes,
		 * we abort sorting. This is large enough to handle all practical scenarios
		 * except in the case of millions of pathname entries each having PATH_MAX length!
		 */
		if (a_state.pathlist_size + a_state.bufpos >= UINT_MAX) {
			log_msg(LOG_WARN, 0, "Too many pathnames. Continuing without sorting.");
			a_state.srt = a_state.head;
			while (a_state.srt) {
				struct sort_buf *srt;
				srt = a_state.srt->next;
				free(a_state.srt);
				a_state.srt = srt;
				goto cont;
			}
		}
		member = &(a_state.srt->members[a_state.srt_pos++]);
		member->size = sb->st_size;
		member->file_pos = a_state.pathlist_size + a_state.bufpos;
		dot = strrchr(basename, '.');

		// Small NAMELEN so these loops will be unrolled by compiler.
		if (tflag != FTW_DP) {
			/*
			 * If not a directory then we store upto first 4 chars of
			 * the extension, if present, or first 4 chars of the
			 * filename.
			 *
			 * NOTE: In order to separate files with and without extensions
			 *       we set the MSB of the size parameter to 1 for extension
			 *       and 0 for no extension. This limits the noted size of the
			 *       file to INT64_MAX, but I think that is more than enough!
			 */
			for (i = 0; i < NAMELEN; i++) member->name[i] = 0;

			i = 0;
			if (!dot) {
				while (basename[i] != '\0' && i < NAMELEN) {
					member->name[i] = basename[i]; i++;
				}
				// Clear 64-bit MSB
				member->size &= 0x7FFFFFFFFFFFFFFF;
			} else {
				dot++;
				while (dot[i] != '\0' && i < NAMELEN) {
					member->name[i] = dot[i]; i++;
				}
				member->size |= 0x8000000000000000;
			}
		} else {
			/*
			 * If this is directory then we store 0xff in the 4 bytes
			 * and invert the size value. This is done to cause directories
			 * to be always sorted after other pathname entries and to
			 * be sorted in descending order of nesting depth.
			 * If we are extracting all permissions then read-only directory
			 * permissions cannot be set before all their child members are
			 * extracted. The following ensures directories are sorted after
			 * other pathnames and they are sorted in descending order of
			 * their nesting depth.
			 */
			for (i = 0; i < NAMELEN; i++) member->name[i] = 255;
			member->size = INT64_MAX - ftwbuf->level;

			/*
			 * Set 64-bit MSB to force directories to be bunched at the end.
			 */
			member->size |= 0x8000000000000000;
		}
	}
cont:
	buf = a_state.pbuf + a_state.bufpos;
	*((short *)buf) = len;
	buf += 2;
	memcpy(buf, fpath, len);
	a_state.bufpos += (len + 2);
	a_state.fcount++;
	return (0);
}

/*
 * Archiving related functions.
 * This one creates a list of files to be included into the archive and
 * sets up the libarchive context.
 */
int
setup_archiver(pc_ctx_t *pctx, struct stat *sbuf)
{
	char *tmpfile, *tmp;
	int err, fd;
	uchar_t *pbuf;
	struct archive *arc;
	struct fn_list *fn;

	/*
	 * If sorting is enabled create the initial sort buffer.
	 */
	if (pctx->enable_archive_sort) {
		struct sort_buf *srt;
		srt = (struct sort_buf *)malloc(sizeof (struct sort_buf));
		if (srt == NULL) {
			log_msg(LOG_ERR, 0, "Out of memory.");
			return (-1);
		}
		srt->next = NULL;
		srt->pos = 0;
		pctx->archive_sort_buf = srt;
	}

	/*
	 * Create a temporary file to hold the generated list of pathnames to be archived.
	 * Storing in a file saves memory usage and allows scalability.
	 */
	tmpfile = pctx->archive_members_file;
	tmp = get_temp_dir();
	strcpy(tmpfile, tmp);
	free(tmp);

	strcat(tmpfile, "/.pcompXXXXXX");
	if ((fd = mkstemp(tmpfile)) == -1) {
		log_msg(LOG_ERR, 1, "mkstemp errored.");
		return (-1);
	}

	add_fname(tmpfile);
	pbuf = malloc(pctx->chunksize);
	if (pbuf == NULL) {
		log_msg(LOG_ERR, 0, "Out of memory.");
		close(fd);  unlink(tmpfile);
		return (-1);
	}

	/*
	 * Use nftw() to scan all the directory hierarchies provided on the command
	 * line and generate a consolidated list of pathnames to be archived. By
	 * doing this we can sort the pathnames and estimate the total archive size.
	 * Total archive size is needed by the subsequent compression stages.
	 */
	log_msg(LOG_INFO, 0, "Scanning files.");
	sbuf->st_size = 0;
	pctx->archive_size = 0;
	pctx->archive_members_count = 0;

	/*
	 * nftw requires using global state variable. So we lock to be mt-safe.
	 * This means only one directory tree scan can happen at a time.
	 */
	pthread_mutex_lock(&nftw_mutex);
	fn = pctx->fn;
	a_state.pbuf = pbuf;
	a_state.bufsiz = pctx->chunksize;
	a_state.bufpos = 0;
	a_state.fd = fd;
	a_state.srt = pctx->archive_sort_buf;
	a_state.srt_pos = 0;
	a_state.head = a_state.srt;
	a_state.pathlist_size = 0;

	while (fn) {
		struct stat sb;

		if (lstat(fn->filename, &sb) == -1) {
			log_msg(LOG_ERR, 1, "Ignoring %s.", fn->filename);
			fn = fn->next;
			continue;
		}

		a_state.arc_size = 0;
		a_state.fcount = 0;
		if (S_ISDIR(sb.st_mode)) {
			/*
			 * Depth-First scan, FTW_DEPTH, is needed to handle restoring
			 * all directory permissions correctly.
			 */
			err = nftw(fn->filename, add_pathname, 1024, FTW_PHYS | FTW_DEPTH);
		} else {
			int tflag;
			struct FTW ftwbuf;
			char *pos;

			if (S_ISLNK(sb.st_mode))
				tflag = FTW_SL;
			else
				tflag = FTW_F;

			/*
			 * Find out basename to mimic FTW.
			 */
			pos = strrchr(fn->filename, PATHSEP_CHAR);
			if (pos)
				ftwbuf.base = pos - fn->filename + 1;
			else
				ftwbuf.base = 0;
			add_pathname(fn->filename, &sb, tflag, &ftwbuf);
			a_state.arc_size = sb.st_size;
		}
		if (a_state.bufpos > 0) {
			ssize_t wrtn = Write(a_state.fd, a_state.pbuf, a_state.bufpos);
			if (wrtn < a_state.bufpos) {
				log_msg(LOG_ERR, 1, "Write failed.");
				close(fd);  unlink(tmpfile);
				return (-1);
			}
			a_state.bufpos = 0;
			a_state.pathlist_size += wrtn;
		}
		pctx->archive_size += a_state.arc_size;
		pctx->archive_members_count += a_state.fcount;
		fn = fn->next;
	}

	if (a_state.srt == NULL) {
		pctx->enable_archive_sort = 0;
	} else {
		log_msg(LOG_INFO, 0, "Sorting ...");
		a_state.srt->max = a_state.srt_pos - 1;
		qsort(a_state.srt->members, a_state.srt_pos, sizeof (member_entry_t), compare_members);
		pctx->archive_temp_size = a_state.pathlist_size;
	}
	pthread_mutex_unlock(&nftw_mutex);

	sbuf->st_size = pctx->archive_size;
	lseek(fd, 0, SEEK_SET);
	free(pbuf);
	sbuf->st_uid = geteuid();
	sbuf->st_gid = getegid();
	sbuf->st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	arc = archive_write_new();
	if (!arc) {
		log_msg(LOG_ERR, 1, "Unable to create libarchive context.\n");
		close(fd);
		unlink(tmpfile);
		return (-1);
	}

	if (pctx->meta_stream)
		archive_set_metadata_streaming(arc, 1);
	archive_write_set_format_pax_restricted(arc);
	archive_write_set_bytes_per_block(arc, 0);
	archive_write_open(arc, pctx, arc_open_callback,
			   creat_write_callback, creat_close_callback);
	pctx->archive_ctx = arc;
	pctx->archive_members_fd = fd;
	if (pctx->enable_archive_sort) {
		pctx->temp_mmap_len = TEMP_MMAP_SIZE;
		pctx->temp_mmap_buf = mmap(NULL, pctx->temp_mmap_len, PROT_READ,
					MAP_SHARED, pctx->archive_members_fd, 0);
		if (pctx->temp_mmap_buf == NULL) {
			log_msg(LOG_WARN, 1, "Unable to mmap pathlist file, switching to read().");
			pctx->temp_mmap_len = 0;
		}
	} else {
		pctx->temp_mmap_buf = NULL;
		pctx->temp_mmap_len = 0;
	}
	pctx->temp_mmap_pos = 0;
	pctx->arc_writing = 0;

	return (0);
}

/*
 * This creates a libarchive context for extracting members to disk.
 */
int
setup_extractor(pc_ctx_t *pctx)
{
	int pipefd[2];
	struct archive *arc;

	if (pipe(pipefd) == -1) {
		log_msg(LOG_ERR, 1, "Unable to create extractor pipe.\n");
		return (-1);
	}

	arc = archive_read_new();
	if (!arc) {
		log_msg(LOG_ERR, 1, "Unable to create libarchive context.\n");
		close(pipefd[0]); close(pipefd[1]);
		return (-1);
	}
	if (pctx->meta_stream)
		archive_set_metadata_streaming(arc, 1);
	archive_read_support_format_all(arc);
	pctx->archive_ctx = arc;
	pctx->arc_writing = 0;

	return (0);
}

static ssize_t
process_by_filter(int fd, int *typ, struct archive *target_arc,
    struct archive *source_arc, struct archive_entry *entry, int cmp,
    int level)
{
	struct filter_info fi;
	int64_t wrtn;

	fi.source_arc = source_arc;
	fi.target_arc = target_arc;
	fi.entry = entry;
	fi.fd = fd;
	fi.compressing = cmp;
	fi.block_size = AW_BLOCK_SIZE;
	fi.type_ptr = typ;
	fi.cmp_level = level;
	wrtn = (*(typetab[(*typ >> 3)].filter_func))(&fi, typetab[(*typ >> 3)].filter_private);
	if (wrtn == FILTER_RETURN_ERROR) {
		log_msg(LOG_ERR, 0, "Error invoking filter module: %s",
		    typetab[(*typ >> 3)].filter_name);
	}
	return (wrtn);
}

static int
write_header(struct archive *arc, struct archive_entry *entry)
{
	int rv;

	rv = archive_write_header(arc, entry);
	if (rv != ARCHIVE_OK) {
		if (rv == ARCHIVE_FATAL || rv == ARCHIVE_FAILED) {
			log_msg(LOG_ERR, 0, "%s: %s",
			    archive_entry_sourcepath(entry), archive_error_string(arc));
			return (-1);
		} else {
			log_msg(LOG_WARN, 0, "%s: %s",
			    archive_entry_sourcepath(entry), archive_error_string(arc));
		}
	}
	return (0);
}

/*
 * Routines to archive members and write the file data to the callback. Portions of
 * the following code is adapted from some of the Libarchive bsdtar code.
 */
static int
copy_file_data(pc_ctx_t *pctx, struct archive *arc, struct archive_entry *entry, int typ)
{
	size_t sz, offset, len;
	ssize_t bytes_to_write;
	uchar_t *mapbuf;
	int rv, fd, typ1;
	const char *fpath;

	typ1 = typ;
	offset = 0;
	rv = 0;
	sz = archive_entry_size(entry);
	bytes_to_write = sz;
	fpath = archive_entry_sourcepath(entry);
	fd = open(fpath, O_RDONLY);
	if (fd == -1) {
		log_msg(LOG_ERR, 1, "Failed to open %s.", fpath);
		return (-1);
	}

	if (typ != TYPE_UNKNOWN) {
		if (typetab[(typ >> 3)].filter_func != NULL) {
			int64_t rv;
			char *fname = typetab[(typ >> 3)].filter_name;

			archive_entry_xattr_add_entry(entry, FILTER_XATTR_ENTRY,
			    fname, strlen(fname));
			if (write_header(arc, entry) == -1) {
				close(fd);
				return (-1);
			}
			pctx->ctype = typ;
			rv = process_by_filter(fd, &(pctx->ctype), arc, NULL, entry,
			    1, pctx->level);
			if (rv == FILTER_RETURN_ERROR) {
				close(fd);
				return (-1);
			} else if (rv != FILTER_RETURN_SKIP) {
				close(fd);
				return (ARCHIVE_OK);
			}
		} else {
			if (write_header(arc, entry) == -1) {
				close(fd);
				return (-1);
			}
		}
	}

	/*
	 * Use mmap for copying file data. Not necessarily for performance, but it saves on
	 * resident memory use.
	 */
	while (bytes_to_write > 0) {
		uchar_t *src;
		size_t wlen;
		ssize_t wrtn;

		if (bytes_to_write < MMAP_SIZE)
			len = bytes_to_write;
		else
			len = MMAP_SIZE;
do_map:
		mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, offset);
		if (mapbuf == NULL) {
			/* Mmap failed; this is bad. */
			log_msg(LOG_ERR, 1, "Mmap failed for %s.", fpath);
			rv = -1;
			break;
		}
		offset += len;
		src = mapbuf;
		wlen = len;

		if (typ == TYPE_UNKNOWN) {
			pctx->ctype = detect_type_by_data(src, len);
			typ = pctx->ctype;
			if (typ != TYPE_UNKNOWN) {
				if (typetab[(typ >> 3)].filter_func != NULL) {
					int64_t rv;
					char *fname = typetab[(typ >> 3)].filter_name;

					archive_entry_xattr_add_entry(entry, FILTER_XATTR_ENTRY,
					    fname, strlen(fname));
					if (write_header(arc, entry) == -1) {
						close(fd);
						return (-1);
					}

					munmap(mapbuf, len);
					rv = process_by_filter(fd, &(pctx->ctype), arc, NULL, entry,
					    1, pctx->level);
					if (rv == FILTER_RETURN_ERROR) {
						return (-1);
					} else if (rv == FILTER_RETURN_SKIP) {
						lseek(fd, 0, SEEK_SET);
						typ = TYPE_COMPRESSED;
						offset = 0;
						goto do_map;
					} else {
						return (ARCHIVE_OK);
					}
				} else {
					if (write_header(arc, entry) == -1) {
						close(fd);
						return (-1);
					}
				}
			} else {
				if (write_header(arc, entry) == -1) {
					close(fd);
					return (-1);
				}
			}
		}
		typ = TYPE_COMPRESSED; // Need to avoid calling detect_type_by_data subsequently.

		/*
		 * Write the entire mmap-ed buffer. Since we are writing to the compressor
		 * stage there is no need for blocking.
		 */
		wrtn = archive_write_data(arc, src, wlen);
		if (wrtn < (ssize_t)wlen) {
			/* Write failed; this is bad */
			log_msg(LOG_ERR, 0, "Data write error: %s", archive_error_string(arc));
			rv = -1;
		}
		bytes_to_write -= wrtn;
		if (rv == -1) break;
		munmap(mapbuf, len);
	}
	close(fd);

	return (rv);
}

static int
write_entry(pc_ctx_t *pctx, struct archive *arc, struct archive_entry *entry, int typ)
{
	/*
	 * If entry has data we postpone writing the header till we have
	 * determined whether the entry type has an associated filter.
	 */
	if (archive_entry_size(entry) > 0) {
		return (copy_file_data(pctx, arc, entry, typ));
	} else {
		if (write_header(arc, entry) == -1)
			return (-1);
	}

	return (0);
}

/*
 * Thread function. Archive members and write to pipe. The dispatcher thread
 * reads from the other end and compresses.
 */
static void *
archiver_thread_func(void *dat) {
	pc_ctx_t *pctx = (pc_ctx_t *)dat;
	char fpath[PATH_MAX], *name, *bnchars = NULL; // Silence compiler
	int warn, rbytes, fpathlen = 0; // Silence compiler
	uint32_t ctr;
	struct archive_entry *entry, *spare_entry, *ent;
	struct archive *arc, *ard;
	struct archive_entry_linkresolver *resolver;
	int readdisk_flags;

	warn = 1;
	entry = archive_entry_new();
	arc = (struct archive *)(pctx->archive_ctx);

	if ((resolver = archive_entry_linkresolver_new()) != NULL) {
		archive_entry_linkresolver_set_strategy(resolver, archive_format(arc));
	} else {
		log_msg(LOG_WARN, 0, "Cannot create link resolver, hardlinks will be duplicated.");
	}

	ctr = 1;
	readdisk_flags = ARCHIVE_READDISK_NO_TRAVERSE_MOUNTS;
	readdisk_flags |= ARCHIVE_READDISK_HONOR_NODUMP;

	ard = archive_read_disk_new();
	archive_read_disk_set_behavior(ard, readdisk_flags);
	archive_read_disk_set_standard_lookup(ard);
	archive_read_disk_set_symlink_physical(ard);

	/*
	 * Read next path entry from list file. read_next_path() also handles sorted reading.
	 */
	while ((rbytes = read_next_path(pctx, fpath, &bnchars, &fpathlen)) != 0) {
		int typ;

		if (rbytes == -1) break;
		archive_entry_copy_sourcepath(entry, fpath);
		if (archive_read_disk_entry_from_file(ard, entry, -1, NULL) != ARCHIVE_OK) {
			log_msg(LOG_WARN, 1, "archive_read_disk_entry_from_file:\n  %s", archive_error_string(ard));
			archive_entry_clear(entry);
			continue;
		}

		typ = TYPE_UNKNOWN;
		if (archive_entry_filetype(entry) == AE_IFREG) {
			if ((typ = detect_type_by_ext(fpath, fpathlen)) != TYPE_UNKNOWN)
				pctx->ctype = typ;
		}

		/*
		 * Strip leading '/' or '../' or '/../' from member name.
		 */
		name = fpath;
		while (name[0] == '/' || name[0] == '\\') {
			if (warn) {
				log_msg(LOG_WARN, 0, "Converting absolute paths.");
				warn = 0;
			}
			if (name[1] == '.' && name[2] == '.' && (name[3] == '/' || name[3] == '\\')) {
				name += 3; /* /.. is removed here and / is removed next. */
			} else {
				name += 1;
			}
		}

#ifndef	__APPLE__
		/*
		 * Workaround for libarchive weirdness on Non MAC OS X platforms. The files
		 * with names matching pattern: ._* are MAC OS X resource forks which contain
		 * extended attributes, ACLs etc. They should be handled accordingly on MAC
		 * platforms and treated as normal files on others. For some reason beyond me
		 * libarchive refuses to extract these files on Linux, no matter what I try.
		 * Bug?
		 * 
		 * In this case the file basename is changed and a custom flag is set to
		 * indicate extraction to change it back.
		 */
		if (bnchars[0] == '.' && bnchars[1] == '_' && archive_entry_filetype(entry) == AE_IFREG) {
			char *pos = strstr(name, "._");
			char name[] = "@.", value[] = "m";
			if (pos) {
				*pos = '|';
				archive_entry_xattr_add_entry(entry, name, value, strlen(value));
			}
		}
#endif

		if (name != archive_entry_pathname(entry))
			archive_entry_copy_pathname(entry, name);

		if (archive_entry_filetype(entry) != AE_IFREG) {
			archive_entry_set_size(entry, 0);
		} else {
			archive_entry_set_size(entry, archive_entry_size(entry));
		}
		log_msg(LOG_VERBOSE, 0, "%5d/%d %8" PRIu64 " %s", ctr, pctx->archive_members_count,
		    archive_entry_size(entry), name);

		archive_entry_linkify(resolver, &entry, &spare_entry);
		ent = entry;
		while (ent != NULL) {
			if (write_entry(pctx, arc, ent, typ) != 0) {
				goto done;
			}
			ent = spare_entry;
			spare_entry = NULL;
		}
		archive_write_finish_entry(arc);
		archive_entry_clear(entry);
		ctr++;
	}

done:
	if (pctx->temp_mmap_len > 0)
		munmap(pctx->temp_mmap_buf, pctx->temp_mmap_len);
	archive_entry_free(entry);
	archive_entry_linkresolver_free(resolver);
	archive_read_free(ard);
	archive_write_free(arc);
	close(pctx->archive_members_fd);
	unlink(pctx->archive_members_file);
	return (NULL);
}

int
start_archiver(pc_ctx_t *pctx) {
	return (pthread_create(&(pctx->archive_thread), NULL, archiver_thread_func, (void *)pctx));
}

/*
 * The next two functions are from libArchive source/example:
 * https://github.com/libarchive/libarchive/wiki/Examples#wiki-A_Complete_Extractor
 *
 * We have to use low-level APIs to extract entries to disk. Normally one would use
 * archive_read_extract2() but LibArchive has no option to set user-defined filter
 * routines, so we have to handle here.
 */
static int
copy_data_out(struct archive *ar, struct archive *aw, struct archive_entry *entry,
    int typ, pc_ctx_t *pctx)
{
	int64_t offset;
	const void *buff;
	size_t size;
	int r;

	if (typ != TYPE_UNKNOWN) {
		if (typetab[(typ >> 3)].filter_func != NULL) {
			int64_t rv;

			rv = process_by_filter(-1, &typ, aw, ar, entry, 0, 0);
			if (rv == FILTER_RETURN_ERROR) {
				archive_set_error(ar, archive_errno(aw),
				    "%s", archive_error_string(aw));
				return (ARCHIVE_FATAL);

			} else if (rv == FILTER_RETURN_SKIP) {
				log_msg(LOG_WARN, 0, "Filter function skipped.");
				return (ARCHIVE_WARN);

			} else if (rv == FILTER_RETURN_SOFT_ERROR) {
				log_msg(LOG_WARN, 0, "Filter function failed for entry: %s.",
				    archive_entry_pathname(entry));
				pctx->errored_count++;
				if (pctx->err_paths_fd) {
					fprintf(pctx->err_paths_fd, "%s,%s",
					    archive_entry_pathname(entry),
					    typetab[(typ >> 3)].filter_name);
				}
				return (ARCHIVE_WARN);
			} else {
				return (ARCHIVE_OK);
			}
		}
	}

	for (;;) {
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return (ARCHIVE_OK);
		if (r != ARCHIVE_OK)
			return (r);
		r = (int)archive_write_data_block(aw, buff, size, offset);
		if (r < ARCHIVE_WARN)
			r = ARCHIVE_WARN;
		if (r != ARCHIVE_OK) {
			archive_set_error(ar, archive_errno(aw),
			    "%s", archive_error_string(aw));
			return (r);
		}
	}
	return (ARCHIVE_OK);
}

static int
archive_extract_entry(struct archive *a, struct archive_entry *entry,
    struct archive *ad, int typ, pc_ctx_t *pctx)
{
	int r, r2;
	char *filter_name;
	size_t name_size;

	/*
	 * If the entry is tagged with our custom xattr we get the filter which
	 * processed it and set the proper type tag.
	 */
	if (archive_entry_has_xattr(entry, FILTER_XATTR_ENTRY,
	    (const void **)&filter_name, &name_size))
	{
		typ = type_tag_from_filter_name(typetab, filter_name, name_size);
		archive_entry_xattr_delete_entry(entry, FILTER_XATTR_ENTRY);
	}
	r = archive_write_header(ad, entry);
	if (r < ARCHIVE_WARN)
		r = ARCHIVE_WARN;
	if (r != ARCHIVE_OK) {
		/* If _write_header failed, copy the error. */
		archive_copy_error(a, ad);
	} else if (!archive_entry_size_is_set(entry) || archive_entry_size(entry) > 0) {
		/* Otherwise, pour data into the entry. */
		r = copy_data_out(a, ad, entry, typ, pctx);
	}
	r2 = archive_write_finish_entry(ad);
	if (r2 < ARCHIVE_WARN)
		r2 = ARCHIVE_WARN;
	/* Use the first message. */
	if (r2 != ARCHIVE_OK && r == ARCHIVE_OK)
		archive_copy_error(a, ad);
	/* Use the worst error return. */
	if (r2 < r)
		r = r2;
	return (r);
}

static int
copy_data_skip(struct archive *ar, struct archive_entry *entry, int typ)
{
	int64_t offset;
	const void *buff;
	size_t size;
	int r;

	for (;;) {
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return (ARCHIVE_OK);
		if (r != ARCHIVE_OK)
			return (r);
	}
	return (ARCHIVE_OK);
}

static int
archive_list_entry(struct archive *a, struct archive_entry *entry, int typ)
{
	time_t tm;
	int tm_is_set = 0;
	char strtm[13];

	if (archive_entry_mtime_is_set(entry)) {
		tm = archive_entry_mtime(entry);
		tm_is_set = 1;

	} else if (archive_entry_atime_is_set(entry)) {
		tm = archive_entry_atime(entry);
		tm_is_set = 1;

	} else if (archive_entry_ctime_is_set(entry)) {
		tm = archive_entry_ctime(entry);
		tm_is_set = 1;

	} else if (archive_entry_birthtime_is_set(entry)) {
		tm = archive_entry_birthtime(entry);
		tm_is_set = 1;
	}

	if (!tm_is_set) {
		strcpy(strtm, "N/A");
	} else {
		if (strftime(strtm, sizeof (strtm), "%b %e %G", localtime(&tm)) == 0)
			strcpy(strtm, "N/A");
	}

	if (archive_entry_size_is_set(entry)) {
		int64_t sz = archive_entry_size(entry);
		printf("%12" PRId64 " %13s %s\n", sz, strtm, archive_entry_pathname(entry));
		if (sz > 0)
			return (copy_data_skip(a, entry, typ));
	} else {
		printf("%12" PRId64 " %13s %s\n", 0LL, strtm, archive_entry_pathname(entry));
	}
	return (ARCHIVE_OK);
}

/*
 * Extract Thread function. Read an uncompressed archive from the decompressor stage
 * and extract members to disk.
 */
static void *
extractor_thread_func(void *dat) {
	pc_ctx_t *pctx = (pc_ctx_t *)dat;
	char cwd[PATH_MAX], got_cwd;
	int flags, rv;
	uint32_t ctr;
	struct archive_entry *entry;
	struct archive *awd, *arc;

	/* Silence compiler. */
	awd = NULL;
	got_cwd = 0;

	if (!pctx->list_mode) {
		flags = ARCHIVE_EXTRACT_TIME;
		flags |= ARCHIVE_EXTRACT_SECURE_SYMLINKS;
		flags |= ARCHIVE_EXTRACT_SECURE_NODOTDOT;
		flags |= ARCHIVE_EXTRACT_SPARSE;

		/*
		 * Extract all security attributes if we are root.
		 */
		if (pctx->force_archive_perms || geteuid() == 0) {
			if (geteuid() == 0)
				flags |= ARCHIVE_EXTRACT_OWNER;
			flags |= ARCHIVE_EXTRACT_PERM;
			flags |= ARCHIVE_EXTRACT_ACL;
			flags |= ARCHIVE_EXTRACT_XATTR;
			flags |= ARCHIVE_EXTRACT_FFLAGS;
			flags |= ARCHIVE_EXTRACT_MAC_METADATA;
		}

		if (pctx->no_overwrite_newer)
			flags |= ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER;

		got_cwd = 1;
		if (getcwd(cwd, PATH_MAX) == NULL) {
			log_msg(LOG_WARN, 1, "Cannot get current directory.");
			got_cwd = 0;
		}

		awd = archive_write_disk_new();
		archive_write_disk_set_options(awd, flags);
		archive_write_disk_set_standard_lookup(awd);
	}
	ctr = 1;
	arc = (struct archive *)(pctx->archive_ctx);
	archive_read_open(arc, pctx, arc_open_callback, extract_read_callback, extract_close_callback);

	/*
	 * Change directory after opening the archive, otherwise archive_read_open() can fail
	 * for relative paths.
	 */
	if (!pctx->list_mode) {
		if (chdir(pctx->to_filename) == -1) {
			log_msg(LOG_ERR, 1, "Cannot change to dir: %s", pctx->to_filename);
			goto done;
		}

		/*
		 * Open list file for pathnames that had filter errors (if any).
		 */
		pctx->err_paths_fd = fopen("filter_failures.txt", "w");
	}

	/*
	 * Read archive entries and extract to disk.
	 */
	while ((rv = archive_read_next_header(arc, &entry)) != ARCHIVE_EOF) {
#ifndef	__APPLE__
		const char *xt_name, *xt_value;
		size_t xt_size;
#endif
		int typ;

		if (rv != ARCHIVE_OK)
			log_msg(LOG_WARN, 0, "%s", archive_error_string(arc));

		if (rv == ARCHIVE_FATAL) {
			log_msg(LOG_ERR, 0, "Fatal error aborting extraction.");
			break;
		}

		if (rv == ARCHIVE_RETRY) {
			log_msg(LOG_INFO, 0, "Retrying extractor read ...");
			continue;
		}

		typ = TYPE_UNKNOWN;
		if (archive_entry_filetype(entry) == AE_IFREG) {
			const char *fpath = archive_entry_pathname(entry);
			typ = detect_type_by_ext(fpath, strlen(fpath));
		}

		/*
		 * Workaround for libarchive weirdness on Non MAC OS X platforms for filenames
		 * starting with '._'. See above ...
		 */
#ifndef	__APPLE__
		if (archive_entry_xattr_reset(entry) > 0) {
			while (archive_entry_xattr_next(entry, &xt_name, (const void **)&xt_value,
			    &xt_size) == ARCHIVE_OK) {
				if (xt_name[0] == '@' && xt_name[1] == '.' && xt_value[0] == 'm') {
					const char *name;
					char *pos;
					name = archive_entry_pathname(entry);
					pos = strstr(name, "|_");
					if (pos) {
						*pos = '.';
						archive_entry_set_pathname(entry, name);
					}
					archive_entry_xattr_clear(entry);
					break;
				}
			}
		}
#endif

		if (!pctx->list_mode) {
			rv = archive_extract_entry(arc, entry, awd, typ, pctx);
		} else {
			rv = archive_list_entry(arc, entry, typ);
		}
		if (rv != ARCHIVE_OK) {
			log_msg(LOG_WARN, 0, "%s: %s", archive_entry_pathname(entry),
			    archive_error_string(arc));

		} else {
			log_msg(LOG_VERBOSE, 0, "%5d %8" PRIu64 " %s", ctr, archive_entry_size(entry),
			    archive_entry_pathname(entry));
		}

		if (rv == ARCHIVE_FATAL) {
			log_msg(LOG_ERR, 0, "Fatal error aborting extraction.");
			break;
		}
		ctr++;
	}

	if (!pctx->list_mode) {
		if (pctx->errored_count > 0) {
			log_msg(LOG_WARN, 0, "WARN: %d pathnames failed filter decoding.");
			if (pctx->err_paths_fd) {
				fclose(pctx->err_paths_fd);
				log_msg(LOG_WARN, 0, "Please see file filter_failures.txt.");
			}
		} else {
			if (pctx->err_paths_fd) {
				fclose(pctx->err_paths_fd);
				(void) unlink("filter_failures.txt");
			}
		}

		if (got_cwd) {
			rv = chdir(cwd);
		}
	}
	archive_read_free(arc);
	archive_write_free(awd);

done:
	return (NULL);
}

int
start_extractor(pc_ctx_t *pctx) {
	return (pthread_create(&(pctx->archive_thread), NULL, extractor_thread_func, (void *)pctx));
}

/*
 * Initialize the hash table of known extensions and types. Bob Jenkins Minimal Perfect Hash
 * is used to get a perfect hash function for the set of known extensions. See:
 * http://burtleburtle.net/bob/hash/perfect.html
 */
int
init_archive_mod() {
	int rv = 0;

	pthread_mutex_lock(&init_mutex);
	if (!inited) {
		int i, j;

		exthtab = malloc(PHASHNKEYS * sizeof (struct ext_hash_entry));
		if (exthtab != NULL) {
			for (i = 0; i < PHASHNKEYS; i++) {
				uint64_t extnum;
				ub4 slot = phash(extlist[i].ext, extlist[i].len);
				extnum = 0;

				/*
				 * Since extensions are less than 8 bytes (or truncated otherwise),
				 * each extension string is packed into a 64-bit integer for quick
				 * comparison.
				 */
				for (j = 0; j < extlist[i].len; j++)
					extnum = (extnum << 8) | extlist[i].ext[j];
				exthtab[slot].extnum = extnum;
				exthtab[slot].type = extlist[i].type;
			}

			memset(typetab, 0, sizeof (typetab));
			inited = 1;
		} else {
			rv = 1;
		}
	}
	pthread_mutex_unlock(&init_mutex);
	return (rv);
}

void
init_filters(struct filter_flags *ff)
{
	pthread_mutex_lock(&init_mutex);
	if (!filters_inited) {
		add_filters_by_type(typetab, ff);
		filters_inited = 1;
	}
	pthread_mutex_unlock(&init_mutex);
}

void
disable_all_filters()
{
	struct filter_flags ff;

	pthread_mutex_lock(&init_mutex);
	if (!filters_inited) {
		ff.enable_packjpg = 0;
		ff.enable_wavpack = 0;
		add_filters_by_type(typetab, &ff);
		filters_inited = 1;
	} else {
		memset(typetab, 0, sizeof (typetab));
	}
	pthread_mutex_unlock(&init_mutex);
}

/*
 * Identify file type based on extension. Lookup is fast as we have a perfect hash function.
 * If the given extension maps to a slot which has a different extension or maps to a slot
 * outside the hash table range then the function returns unknown type.
 */
static int
detect_type_from_ext(const char *ext, int len)
{
	int i;
	ub4 slot;
	char extl[8];
	uint64_t extnum;

	if (len == 0 || len > 8) goto ret; // If extension is empty give up
	for (i = 0; i < len; i++) extl[i] = tolower(ext[i]);
	slot = phash(extl, len);
	if (slot >= PHASHNKEYS) goto ret; // Extension maps outside hash table range, give up
	extnum = 0;

	/*
	 * Pack given extension into 64-bit integer.
	 */
	for (i = 0; i < len; i++)
		extnum = (extnum << 8) | tolower(ext[i]);
	if (exthtab[slot].extnum == extnum)
		return (exthtab[slot].type);
ret:
	return (TYPE_UNKNOWN);
}

static int
detect_type_by_ext(const char *path, int pathlen)
{
	const char *ext = NULL;
	int i, len;

	for (i = pathlen-1; i > 0 && path[i] != '.' && path[i] != PATHSEP_CHAR; i--);
	if (i == 0 || path[i] != '.') goto out; // If extension not found give up
	len = pathlen - i - 1;
	ext = &path[i+1];
	return (detect_type_from_ext(ext, len));
out:
	return (TYPE_UNKNOWN);
}

#ifdef WORDS_BIGENDIAN
/* 0x7fELF packed into 32-bit integer. */
#	define	ELFINT (0x7f454c46U)

/* TZif packed into 32-bit integer. */
#	define	TZSINT	(0x545a6966U)

/* PPMZ packed into 32-bit integer. */
#	define	PPMINT	(0x50504d5aU)

/* wvpk packed into 32-bit integer. */
#	define	WVPK	(0x7776706b)

/* TTA1 packed into 32-bit integer. */
#	define	TTA1	(0x54544131)

/* Magic for different MSDOS COM file types. */
#	define COM_MAGIC	(0xcd21)
#else
/* 0x7fELF packed into 32-bit integer. */
#	define	ELFINT (0x464c457fU)

/* TZif packed into 32-bit integer. */
#	define	TZINT	(0x66695a54U)

/* PPMZ packed into 32-bit integer. */
#	define	PPMINT	(0x5a4d5050U)

/* wvpk packed into 32-bit integer. */
#	define	WVPK	(0x6b707677)

/* TTA1 packed into 32-bit integer. */
#	define	TTA1	(0x31415454)

/* Magic for different MSDOS COM file types. */
#	define COM_MAGIC	(0x21cd)
#endif

/*
 * Detect a few file types from looking at magic signatures.
 */
static int
detect_type_by_data(uchar_t *buf, size_t len)
{
	// At least a few bytes.
	if (len < 10) return (TYPE_UNKNOWN);

	// WAV files.
	if (identify_wav_type(buf, len))
		return (TYPE_BINARY|TYPE_WAV);

	if (memcmp(buf, "!<arch>\n", 8) == 0)
		return (TYPE_BINARY|TYPE_ARCHIVE_AR);
	if (memcmp(&buf[257], "ustar\0", 6) == 0 || memcmp(&buf[257], "ustar\040\040\0", 8) == 0)
		return (TYPE_BINARY|TYPE_ARCHIVE_TAR);
	if (memcmp(buf, "%PDF-", 5) == 0)
		return (TYPE_BINARY|TYPE_PDF);

	// Try to detect DICOM medical image file. BSC compresses these better.
	if (len > 127) {
		int i;

		// DICOM files should have either DICM or ISO_IR within the first 128 bytes
		for (i = 0; i < 128-4; i++) {
			if (buf[i] == 'D')
				if (memcmp(&buf[i], "DICM", 4) == 0)
					return (TYPE_BINARY|TYPE_DICOM);
			if (buf[i] == 'I')
				if (memcmp(&buf[i], "ISO_IR ", 7) == 0)
					return (TYPE_BINARY|TYPE_DICOM);
		}
	}

	// Jpegs
	if (len > 9 && buf[0] == 0xFF && buf[1] == 0xD8) {
		if (strncmp((char *)&buf[6], "Exif", 4) == 0 ||
		    strncmp((char *)&buf[6], "JFIF", 4) == 0) {
			return (TYPE_BINARY|TYPE_JPEG);
		}
	}

	if (U32_P(buf) == ELFINT) {  // Regular ELF, check for 32/64-bit, core dump
		if (*(buf + 16) != 4) {
			if (*(buf + 4) == 2) {
				return (TYPE_BINARY|TYPE_EXE64);
			} else {
				return (TYPE_BINARY|TYPE_EXE32);
			}
		} else {
			return (TYPE_BINARY);
		}
	}
	if (buf[1] == 'Z') {
		 // Check for MSDOS/Windows Exe types
		if (buf[0] == 'L') {
			return (TYPE_BINARY|TYPE_EXE32);
		} else if (buf[0] == 'M') {
			// If relocation table is less than 0x40 bytes into file then
			// it is a 32-bit MSDOS exe.
			if (LE16(U16_P(buf + 0x18)) < 0x40) {
				return (TYPE_BINARY|TYPE_EXE32);
			} else {
				uint32_t off = LE32(U32_P(buf + 0x3c));
				// This is non-MSDOS, check whether PE
				if (off < len - 3) {
					if (buf[off] == 'P' && buf[off+1] == 'E' &&
					    buf[off+2] == '\0' && buf[off+3] == '\0') {
						// This is a PE executable.
						// Check 32/64-bit.
						off = LE32(U32_P(buf + 0x3c))+4;
						if (LE16(U16_P(buf + off)) == 0x8664) {
							return (TYPE_BINARY|TYPE_EXE64);
						} else {
							return (TYPE_BINARY|TYPE_EXE32);
						}
					} else {
						return (TYPE_BINARY|TYPE_EXE32);
					}
				}
			}
		}
	}

	// BMP Files
	if (buf[0] == 'B' && buf[1] == 'M') {
		uint16_t typ = LE16(U16_P(buf + 14));
		if (typ == 12 || typ == 64 || typ == 40 || typ == 128)
			return (TYPE_BINARY|TYPE_BMP);
	}

	if (U32_P(buf) == TZINT)
		return (TYPE_BINARY); // Timezone data
	if (U32_P(buf) == PPMINT)
		return (TYPE_BINARY|TYPE_COMPRESSED|TYPE_COMPRESSED_PPMD); // PPM Compressed archive
	if (U32_P(buf) == WVPK || U32_P(buf) == TTA1)
		return (TYPE_BINARY|TYPE_COMPRESSED|TYPE_AUDIO_COMPRESSED);

	// PNM files
	if (identify_pnm_type(buf, len)) {
		return (TYPE_BINARY|TYPE_PNM);
	}

	// MSDOS COM types, two byte and one byte magic numbers are checked
        // after all other multi-byte magic number checks.
	if (buf[0] == 0xe9 || buf[0] == 0xeb) {
		if (LE16(U16_P(buf + 0x1fe)) == 0xaa55)
			return (TYPE_BINARY|TYPE_EXE32); // MSDOS COM
		else
			return (TYPE_BINARY);
	}

	if (U16_P(buf + 2) == COM_MAGIC || U16_P(buf + 4) == COM_MAGIC ||
	    U16_P(buf + 4) == COM_MAGIC || U16_P(buf + 5) == COM_MAGIC ||
	    U16_P(buf + 13) == COM_MAGIC || U16_P(buf + 18) == COM_MAGIC ||
	    U16_P(buf + 23) == COM_MAGIC || U16_P(buf + 30) == COM_MAGIC ||
	    U16_P(buf + 70) == COM_MAGIC) {
			return (TYPE_BINARY|TYPE_EXE32); // MSDOS COM
	}
	return (TYPE_UNKNOWN);
}
