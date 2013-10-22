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
#include <archive.h>
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
	struct hdr ehdr;

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
setup_archive(pc_ctx_t *pctx, struct stat *sbuf)
{
	char *tmpfile, *tmp;
	int err, fd, pipefd[2];
	uchar_t *pbuf;
	struct archive *arc;

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
	pthread_mutex_lock(&nftw_mutex);
	a_state.pbuf = pbuf;
	a_state.bufsiz = pctx->chunksize;
	a_state.bufpos = 0;
	a_state.arc_size = 0;
	a_state.fd = fd;
	err = nftw(pctx->filename, add_pathname, 1024, FTW_PHYS); // 'pctx->filename' has dir name here
	if (a_state.bufpos > 0) {
		ssize_t wrtn = Write(a_state.fd, a_state.pbuf, a_state.bufpos);
		if (wrtn < a_state.bufpos) {
			log_msg(LOG_ERR, 1, "Write failed.");
			close(fd);  unlink(tmpfile);
			return (-1);
		}
		a_state.bufpos = 0;
	}
	pctx->archive_size = a_state.arc_size;
	sbuf->st_size = a_state.arc_size;
	pthread_mutex_unlock(&nftw_mutex);
	lseek(fd, 0, SEEK_SET);
	free(pbuf);

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

/*
 * Thread function. Archive members and write to pipe. The dispatcher thread
 * reads from the other end and compresses.
 */
static void *
archiver_thread(void *dat) {
	pc_ctx_t *pctx = (pc_ctx_t *)dat;
	char fpath[PATH_MAX], *name;
	ssize_t rbytes;
	short namelen;
	int warn;
	struct stat sbuf;
	struct archive_entry *entry;
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
		int fd;

		if (rbytes < 2) break;
		rbytes = Read(pctx->archive_members_fd, fpath, namelen);
		if (rbytes < namelen) break;
		archive_entry_copy_sourcepath(entry, fpath);
		if (archive_read_disk_entry_from_file(ard, entry, 0, NULL) != ARCHIVE_OK) {
			log_msg(LOG_WARN, 0, "%s", archive_error_string(ard);
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
				name += 4; /* /.. is removed here and / is removed next. */
			} else {
				name += 1;
			}
		}
		if (name != archive_entry_pathname(entry))
			archive_entry_copy_pathname(entry, name);

		if (archive_entry_filetype(entry) != AE_IFREG)
			archive_entry_set_size(entry, 0);
		archive_entry_linkify(bsdtar->resolver, &entry, &spare_entry);
		archive_entry_write_header(arc, entry);
		archive_entry_clear(entry);
	}
	return (NULL);
}

int
start_archiver(pc_ctx_t *pctx) {
	return (0);
}
