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
#include <archive.h>
#include <archive_entry.h>
#include "pc_archive.h"

#undef _FEATURES_H
#define _XOPEN_SOURCE 700
#include <ftw.h>
#include <stdint.h>

/*
AE_IFREG   Regular file
AE_IFLNK   Symbolic link
AE_IFSOCK  Socket
AE_IFCHR   Character device
AE_IFBLK   Block device
AE_IFDIR   Directory
AE_IFIFO   Named pipe (fifo)
*/

#define	ARC_ENTRY_OVRHEAD	500
#define	ARC_SCRATCH_BUFF_SIZE	(64 *1024)
#define	MMAP_SIZE		(1024 * 1024)

static struct arc_list_state {
	uchar_t *pbuf;
	uint64_t bufsiz, bufpos, arc_size;
	int fd;
} a_state;
pthread_mutex_t nftw_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Build list of pathnames in a temp file.
 */
static int
add_pathname(const char *fpath, const struct stat *sb,
                    int tflag, struct FTW *ftwbuf)
{
	short len;
	uchar_t *buf;

	if (tflag == FTW_DP) return (0);
	if (tflag == FTW_DNR || tflag == FTW_NS) {
		log_msg(LOG_WARN, 0, "Cannot access %s\n", fpath);
		return (0);
	}
	a_state.arc_size += (sb->st_size + ARC_ENTRY_OVRHEAD);
	len = strlen(fpath);
	if (a_state.bufpos + len + 14 > a_state.bufsiz) {
		ssize_t wrtn = Write(a_state.fd, a_state.pbuf, a_state.bufpos);
		if (wrtn < a_state.bufpos) {
			log_msg(LOG_ERR, 1, "Write: ");
			return (-1);
		}
		a_state.bufpos = 0;
	}
	buf = a_state.pbuf + a_state.bufpos;
	*((short *)buf) = len;
	buf += 2;
	memcpy(buf, fpath, len);
	a_state.bufpos += (len + 2);
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
	int err, fd, pipefd[2];
	uchar_t *pbuf;
	struct archive *arc;
	struct fn_list *fn;

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
	 * nftw requires using global state variable. So we lock to be mt-safe.
	 * This means only one directory tree scan can happen at a time.
	 */
	log_msg(LOG_INFO, 0, "Scanning files.");
	sbuf->st_size = 0;
	pctx->archive_size = 0;
	pthread_mutex_lock(&nftw_mutex);
	fn = pctx->fn;
	a_state.pbuf = pbuf;
	a_state.bufsiz = pctx->chunksize;
	a_state.bufpos = 0;
	a_state.arc_size = 0;
	a_state.fd = fd;

	while (fn) {
		struct stat sb;

		if (lstat(fn->filename, &sb) == -1) {
			log_msg(LOG_ERR, 1, "Ignoring %s.", fn->filename);
			fn = fn->next;
			continue;
		}

		if (S_ISDIR(sb.st_mode)) {
			err = nftw(fn->filename, add_pathname, 1024, FTW_PHYS);
		} else {
			int tflag;

			if (S_ISLNK(sb.st_mode))
				tflag = FTW_SL;
			else
				tflag = FTW_F;
			add_pathname(fn->filename, &sb, tflag, NULL);
		}
		if (a_state.bufpos > 0) {
			ssize_t wrtn = Write(a_state.fd, a_state.pbuf, a_state.bufpos);
			if (wrtn < a_state.bufpos) {
				log_msg(LOG_ERR, 1, "Write failed.");
				close(fd);  unlink(tmpfile);
				return (-1);
			}
			a_state.bufpos = 0;
		}
		pctx->archive_size += a_state.arc_size;
		fn = fn->next;
	}
	pthread_mutex_unlock(&nftw_mutex);
	sbuf->st_size = pctx->archive_size;
	lseek(fd, 0, SEEK_SET);
	free(pbuf);
	sbuf->st_uid = geteuid();
	sbuf->st_gid = getegid();
	sbuf->st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	if (pipe(pipefd) == -1) {
		log_msg(LOG_ERR, 1, "Unable to create archiver pipe.\n");
		close(fd);  unlink(tmpfile);
		return (-1);
	}

	pctx->uncompfd = pipefd[0]; // Read side
	pctx->archive_data_fd = pipefd[1]; // Write side
	arc = archive_write_new();
	if (!arc) {
		log_msg(LOG_ERR, 1, "Unable to create libarchive context.\n");
		close(fd); close(pipefd[0]); close(pipefd[1]);
		unlink(tmpfile);
		return (-1);
	}
	archive_write_set_format_pax_restricted(arc);
	archive_write_open_fd(arc, pctx->archive_data_fd);
	pctx->archive_ctx = arc;
	pctx->archive_members_fd = fd;

	return (0);
}

int
setup_extractor(pc_ctx_t *pctx)
{
	int pipefd[2];
	struct archive *arc;

	if (pipe(pipefd) == -1) {
		log_msg(LOG_ERR, 1, "Unable to create extractor pipe.\n");
		return (-1);
	}

	pctx->uncompfd = pipefd[1]; // Write side
	pctx->archive_data_fd = pipefd[0]; // Read side

	arc = archive_read_new();
	if (!arc) {
		log_msg(LOG_ERR, 1, "Unable to create libarchive context.\n");
		close(pipefd[0]); close(pipefd[1]);
		return (-1);
	}
	archive_read_support_format_all(arc);
	pctx->archive_ctx = arc;

	return (0);
}

/*
 * Routines to archive members and write the archive to pipe. Most of the following
 * code is adapted from some of the Libarchive bsdtar code.
 */
static int
copy_file_data(pc_ctx_t *pctx, struct archive *arc,
	       struct archive *in_arc, struct archive_entry *entry)
{
	size_t sz, offset, len;
	ssize_t bytes_to_write;
	uchar_t *mapbuf;
	int rv, fd;
	const char *fpath;

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

	while (bytes_to_write > 0) {
		uchar_t *src;
		size_t wlen;
		ssize_t wrtn;

		if (bytes_to_write < MMAP_SIZE)
			len = bytes_to_write;
		else
			len = MMAP_SIZE;
		mapbuf = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, offset);
		offset += len;
		src = mapbuf;
		wlen = len;

		/*
		 * Write the entire mmap-ed buffer. Since we are writing to the compressor
		 * stage pipe there is no need for blocking.
		 */
		wrtn = archive_write_data(arc, src, wlen);
		if (wrtn < wlen) {
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
write_entry(pc_ctx_t *pctx, struct archive *arc, struct archive *in_arc,
	    struct archive_entry *entry)
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

	if (archive_entry_size(entry) > 0) {
		return (copy_file_data(pctx, arc, in_arc, entry));
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
	char fpath[PATH_MAX], *name;
	ssize_t rbytes;
	short namelen;
	int warn;
	struct archive_entry *entry, *spare_entry, *ent;
	struct archive *arc, *ard;
	struct archive_entry_linkresolver *resolver;

	warn = 1;
	entry = archive_entry_new();
	arc = (struct archive *)(pctx->archive_ctx);

	if ((resolver = archive_entry_linkresolver_new()) != NULL) {
		archive_entry_linkresolver_set_strategy(resolver, archive_format(arc));
	} else {
		log_msg(LOG_WARN, 0, "Cannot create link resolver, hardlinks will be duplicated.");
	}

	ard = archive_read_disk_new();
	archive_read_disk_set_standard_lookup(ard);
	archive_read_disk_set_symlink_physical(ard);

	/*
	 * Read next path entry from list file.
	 */
	while ((rbytes = Read(pctx->archive_members_fd, &namelen, sizeof(namelen))) != 0) {
		if (rbytes < 2) {
			log_msg(LOG_ERR, 1, "Error reading archive members file.");
			break;
		}
		rbytes = Read(pctx->archive_members_fd, fpath, namelen);
		if (rbytes < namelen) {
			log_msg(LOG_ERR, 1, "Error reading archive members file.");
			break;
		}
		fpath[namelen] = '\0';
		archive_entry_copy_sourcepath(entry, fpath);
		if (archive_read_disk_entry_from_file(ard, entry, -1, NULL) != ARCHIVE_OK) {
			log_msg(LOG_WARN, 1, "archive_read_disk_entry_from_file:\n  %s", archive_error_string(ard));
			archive_entry_clear(entry);
			continue;
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
		if (name != archive_entry_pathname(entry))
			archive_entry_copy_pathname(entry, name);

		if (archive_entry_filetype(entry) != AE_IFREG) {
			archive_entry_set_size(entry, 0);
		}
		if (pctx->verbose)
			log_msg(LOG_INFO, 0, "%10d %s", archive_entry_size(entry), name);

		archive_entry_linkify(resolver, &entry, &spare_entry);
		ent = entry;
		while (ent != NULL) {
			if (write_entry(pctx, arc, ard, ent) != 0) {
				goto done;
			}
			ent = spare_entry;
			spare_entry = NULL;
		}
		archive_entry_clear(entry);
	}

done:
	archive_entry_free(entry);
	archive_entry_linkresolver_free(resolver);
	archive_read_free(ard);
	archive_write_free(arc);
	close(pctx->archive_members_fd);
	close(pctx->archive_data_fd);
	unlink(pctx->archive_members_file);
	return (NULL);
}

int
start_archiver(pc_ctx_t *pctx) {
	return (pthread_create(&(pctx->archive_thread), NULL, archiver_thread_func, (void *)pctx));
}

/*
 * Extract Thread function. Read an uncompressed archive from the pipe and extract
 * members to disk. The decompressor writes to the other end of the pipe.
 */
static void *
extractor_thread_func(void *dat) {
	pc_ctx_t *pctx = (pc_ctx_t *)dat;
	char cwd[PATH_MAX], got_cwd;
	int flags, rv;
	struct archive_entry *entry;
	struct archive *awd, *arc;

	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

	got_cwd = 1;
	if (getcwd(cwd, PATH_MAX) == NULL) {
		log_msg(LOG_WARN, 1, "Cannot get current directory.");
		got_cwd = 0;
	}

	if (chdir(pctx->to_filename) == -1) {
		log_msg(LOG_ERR, 1, "Cannot change to dir: %s", pctx->to_filename);
		goto done;
	}

	awd = archive_write_disk_new();
	archive_write_disk_set_options(awd, flags);
	archive_write_disk_set_standard_lookup(awd);
	arc = (struct archive *)(pctx->archive_ctx);
	archive_read_open_fd(arc, pctx->archive_data_fd, MMAP_SIZE);

	/*
	 * Read archive entries and extract to disk.
	 */
	while ((rv = archive_read_next_header(arc, &entry)) != ARCHIVE_EOF) {
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

		rv = archive_read_extract2(arc, entry, awd);
		if (rv != ARCHIVE_OK) {
			log_msg(LOG_WARN, 0, "%s: %s", archive_entry_pathname(entry),
			    archive_error_string(arc));

		} else if (pctx->verbose) {
			log_msg(LOG_INFO, 0, "%10d %s", archive_entry_size(entry),
			    archive_entry_pathname(entry));
		}

		if (rv == ARCHIVE_FATAL) {
			log_msg(LOG_ERR, 0, "Fatal error aborting extraction.");
			break;
		}
	}

	if (got_cwd) {
		rv = chdir(cwd);
	}
	archive_read_free(arc);
	archive_write_free(awd);
done:
	close(pctx->archive_data_fd);
	return (NULL);
}

int
start_extractor(pc_ctx_t *pctx) {
	return (pthread_create(&(pctx->archive_thread), NULL, extractor_thread_func, (void *)pctx));
}
