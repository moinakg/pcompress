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
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 * This program includes partly-modified public domain source
 * code from the LZMA SDK: http://www.7-zip.org/sdk.html
 */

#include <sys/types.h>
#include <sys/param.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <link.h>

#include "utils.h"

void
err_exit(int show_errno, const char *format, ...)
{
	int err = errno;
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	if (show_errno)
		fprintf(stderr, "\nError: %s\n", strerror(err));
	exit(1);
}

/*
 * Fetch the command name that started the current process.
 * The returned string must be freed by the caller.
 */
const char *
get_execname(const char *argv0)
{
	char path[MAXPATHLEN];
	char apath[128];
	char *tmp1, *tmp2;
	pid_t pid;

	/* The easiest case: we are in linux */
	if (readlink("/proc/self/exe", path, MAXPATHLEN) != -1) {
		return (strdup(basename(path)));
	}

	/* Next easy case: Solaris/Illumos */
	pid = getpid();
	sprintf(apath, "/proc/%d/path/a.out", pid);
	if (readlink(apath, path, MAXPATHLEN) != -1) {
		return (strdup(basename(path)));
	}

	/* Oops... not in linux, not in Solaris no  guarantee */
	/* check if we have something like execve("foobar", NULL, NULL) */
	if (argv0 == NULL) {
		/* Give up */
		return (strdup("Unknown"));
	}

	tmp1 = strdup(argv0);
	tmp2 = strdup(basename(tmp1));
	free(tmp1);
	return (tmp2);
}

/*
 * Routines to parse a numeric string which can have the following suffixes:
 * k - Kilobyte
 * m - Megabyte
 * g - Gigabyte
 *
 * The number should fit in an ssize_t data type.
 * Numeric overflow is also checked. The routine parse_numeric() returns
 * 1 if there was a numeric overflow.
 */
static int
raise_by_multiplier(ssize_t *val, int mult, int power) {
	ssize_t result;

	while (power-- > 0) {
		result = *val * mult;
		if (result/mult != *val)
			return (1);
		*val = result;
	}
	return (0);
}

int
parse_numeric(ssize_t *val, const char *str)
{
	int i, ovr;
	char *mult;

	*val = strtoll(str, &mult, 0);
	if (*mult != '\0') {
		switch (*mult) {
		  case 'k':
		  case 'K':
			ovr = raise_by_multiplier(val, 1024, 1);
			break;
		  case 'm':
		  case 'M':
			ovr = raise_by_multiplier(val, 1024, 2);
			break;
		  case 'g':
		  case 'G':
			ovr = raise_by_multiplier(val, 1024, 3);
			break;
		  default:
			ovr = 2;
		}
	}

	return (ovr);
}

/*
 * Convert number of bytes into human readable format
 */
char *
bytes_to_size(uint64_t bytes)
{
	static char num[20];
	uint64_t kilobyte = 1024;
	uint64_t megabyte = kilobyte * 1024;
	uint64_t gigabyte = megabyte * 1024;
	uint64_t terabyte = gigabyte * 1024;

	if (bytes < kilobyte) {
		sprintf(num, "%llu B", bytes);

	} else if (bytes < megabyte) {
		sprintf(num, "%llu KB", bytes / kilobyte);

	} else if (bytes < gigabyte) {
		sprintf(num, "%llu MB", bytes / megabyte);

	} else if (bytes < terabyte) {
		sprintf(num, "%llu GB", bytes / gigabyte);

	} else {
		sprintf(num, "%llu B", bytes);
	}
	return (num);
}

/*
 * Read/Write helpers to ensure a full chunk is read or written
 * unless there is an error.
 */
ssize_t
Read(int fd, void *buf, size_t count)
{
	ssize_t rcount, rem;
	uchar_t *cbuf;

	rem = count;
	cbuf = (uchar_t *)buf;
	do {
		rcount = read(fd, cbuf, rem);
		if (rcount < 0) return (rcount);
		if (rcount == 0) break;
		rem = rem - rcount;
		cbuf += rcount;
	} while (rem);
	return (count - rem);
}

ssize_t
Write(int fd, const void *buf, size_t count)
{
	ssize_t wcount, rem;
	uchar_t *cbuf;

	rem = count;
	cbuf = (uchar_t *)buf;
	do {
		wcount = write(fd, cbuf, rem);
		if (wcount < 0) return (wcount);
		rem = rem - wcount;
		cbuf += wcount;
	} while (rem);
	return (count - rem);
}

