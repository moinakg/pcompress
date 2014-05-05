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

#ifndef _BS_COMMON_
#define _BS_COMMON_

#include <stdio.h>
#include <utils.h>

// Simple stream I/O to buffer
typedef struct {
	uchar_t *buf;
	bsize_t pos;
	bsize_t buflen;
} bufio_t;

static int
BUFOPEN(bufio_t *bio, uchar_t *buf, bsize_t len)
{
	bio->buf = buf; bio->pos = 0; bio->buflen = len;
	return (0);
}

#ifndef	__IN_BSPATCH__
static bsize_t
BUFWRITE(bufio_t *bio, uchar_t *buf, bsize_t len)
{
	if (bio->pos + len < bio->buflen) {
		memcpy(bio->buf + bio->pos, buf, len);
		bio->pos += len;
		return (len);
	} else {
		return (-1);
	}
}
#endif

#ifndef	__IN_BSDIFF__
static bsize_t
BUFREAD(bufio_t *bio, uchar_t *buf, bsize_t len)
{
	bsize_t actual;
	unsigned int tot;

	actual = len;
	tot = len;
	tot += bio->pos;
	if (tot > bio->buflen) {
		actual = bio->buflen - bio->pos;
	}
	if (actual == 0) return (0);
	memcpy(buf, bio->buf + bio->pos, actual);
	bio->pos += actual;
	return (actual);
}
#endif

#ifndef	__IN_BSPATCH__
static bsize_t
BUFTELL(bufio_t *bio)
{
	return (bio->pos);
}

static void *
BUFPTR(bufio_t *bio)
{
	return (bio->buf + bio->pos);
}

static int
BUFSEEK(bufio_t *bio, bsize_t pos, int typ)
{
	if (typ == SEEK_SET) {
		bio->pos = pos;

	} else if (typ == SEEK_CUR) {
		bio->pos += pos;

	} else {
		if (pos > 0) {
			fprintf(stderr, "Cannot seek beyond buffer end.\n");
			return (-1);
		} else {
			bio->pos = bio->buflen + pos;
		}
	}
	return (0);
}
#endif

extern int zero_rle_encode(const void *ibuf, const unsigned int ilen,
	void *obuf, unsigned int *olen);
extern int zero_rle_decode(const void* ibuf, unsigned int ilen,
	void* obuf, unsigned int *olen);

#endif
