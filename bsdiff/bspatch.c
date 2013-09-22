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

/*-
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
__FBSDID("$FreeBSD: src/usr.bin/bsdiff/bspatch/bspatch.c,v 1.1 2005/08/06 01:59:06 cperciva Exp $");
#endif

#include <bzlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <allocator.h>
#include <utils.h>

#define	__IN_BSPATCH__
#include "bscommon.h"

static int32_t
valini32(u_char *buf)
{
	return ntohl(I32_P(buf));
}

bsize_t
get_bsdiff_sz(u_char *pbuf) {
	bsize_t newsize;
	bsize_t lzctrllen, ctrllen, lzdatalen, datalen, lzextralen, extralen;
	int hdrsz;

	hdrsz = 4*7;

	lzctrllen = valini32(pbuf);
	ctrllen = valini32(pbuf+4);
	lzdatalen = valini32(pbuf+4*2);
	datalen = valini32(pbuf+4*3);
	lzextralen = valini32(pbuf+4*4);
	extralen = valini32(pbuf+4*5);
	newsize = valini32(pbuf+4*6);
	return (lzctrllen + lzdatalen + lzextralen + hdrsz);
}

int
bspatch(u_char *pbuf, u_char *oldbuf, bsize_t oldsize, u_char *newbuf, bsize_t *_newsize)
{
	bsize_t newsize;
	bsize_t lzctrllen, ctrllen, lzdatalen, datalen, lzextralen, extralen;
	u_char buf[8];
	u_char *diffdata, *extradata, *ctrldata;
	bsize_t oldpos,newpos;
	bsize_t ctrl[3];
	bsize_t lenread;
	bsize_t i;
	bufio_t cpf, dpf, epf;
	int hdrsz, rv;
	unsigned int len;

	/*
	File format:
		0	4	compressed length of ctrl block (X)
		4	4	actual length of ctrl block (X)
		8	4	compressed length of diff block (Y)
		12	4	actual length of diff block
		16	4	compressed length of extra block (Z)
		20	4	actual length of extra block
		24	4	length of new file
		28	X	ZRLE?(control block)
		28+X	Y	ZRLE(diff block)
		28+X+Y	Z	ZRLE(extra block)
	with control block a set of triples (x,y,z) meaning "add x bytes
	from oldfile to x bytes from the diff block; copy y bytes from the
	extra block; seek forwards in oldfile by z bytes".
	*/
	hdrsz = 4*7;
	rv = 1;

	/* Read lengths from header first. */
	lzctrllen = valini32(pbuf);
	ctrllen = valini32(pbuf+4);
	lzdatalen = valini32(pbuf+4*2);
	datalen = valini32(pbuf+4*3);
	lzextralen = valini32(pbuf+4*4);
	extralen = valini32(pbuf+4*5);
	newsize = valini32(pbuf+4*6);

	if((ctrllen<0) || (lzdatalen<0) || (newsize<0) || (lzextralen<0)) {
		fprintf(stderr, "1: Corrupt patch\n");
		return (0);
	}
	if (newsize > *_newsize) {
		fprintf(stderr, "Output buffer too small.\n");
		return (0);
	}
	*_newsize = newsize;

	/* Allocate buffers. */
	diffdata = (u_char *)slab_alloc(NULL, datalen);
	extradata = (u_char *)slab_alloc(NULL, extralen);
	if (diffdata == NULL || extradata == NULL) {
		fprintf(stderr, "bspatch: Out of memory.\n");
		if (diffdata) slab_free(NULL, diffdata);
		if (extradata) slab_free(NULL, extradata);
		return (0);
	}

	/* Decompress ctrldata, diffdata and extradata. */
	if (lzctrllen < ctrllen) {
		/* Ctrl data will be RLE-d if RLE size is less. */
		ctrldata = (u_char *)slab_alloc(NULL, ctrllen);
		if (ctrldata == NULL) {
			fprintf(stderr, "bspatch: Out of memory.\n");
			slab_free(NULL, diffdata);
			slab_free(NULL, extradata);
			return (0);
		}
		len = ctrllen;
		if (zero_rle_decode(pbuf + hdrsz, lzctrllen, ctrldata, &len) == -1 ||
		    len != ctrllen) {
			fprintf(stderr, "bspatch: Failed to decompress control data.\n");
			rv = 0;
			goto out;
		}
	} else {
		ctrldata = pbuf + hdrsz;
	}

	len = datalen;
	if (zero_rle_decode(pbuf + hdrsz + lzctrllen, lzdatalen, diffdata, &len) == -1 ||
	    len != datalen) {
		fprintf(stderr, "bspatch: Failed to decompress diff data.\n");
		rv = 0;
		goto out;
	}
	datalen = len;

	len = extralen;
	if (len > 0) {
		if (extralen == lzextralen) {
			memcpy(extradata, pbuf + hdrsz + lzctrllen + lzdatalen, lzextralen);

		} else if (zero_rle_decode(pbuf + hdrsz + lzctrllen + lzdatalen, lzextralen, extradata, &len) == -1 ||
		    len != extralen) {
			fprintf(stderr, "bspatch: Failed to decompress extra data.\n");
			rv = 0;
			goto out;
		}
	}

	extralen = len;
	BUFOPEN(&cpf, ctrldata, ctrllen);
	BUFOPEN(&dpf, diffdata, datalen);
	BUFOPEN(&epf, extradata, extralen);

	oldpos=0;newpos=0;
	while(newpos<newsize) {
		/* Read control data */
		for(i=0;i<=2;i++) {
			lenread = BUFREAD(&cpf, buf, 4);
			if (lenread < 4) {
				fprintf(stderr, "2: Corrupt diff data\n");
				rv = 0;
				goto out;
			}
			ctrl[i]=valini32(buf);
		};

		/* Sanity-check */
		if(newpos+ctrl[0]>newsize) {
			fprintf(stderr, "3: Corrupt diff data\n");
			rv = 0;
			goto out;
		}

		/* Read diff string */
		lenread = BUFREAD(&dpf, newbuf + newpos, ctrl[0]);
		if (lenread < ctrl[0]) {
			fprintf(stderr, "4: Corrupt diff data\n");
			rv = 0;
			goto out;
		}

		/* Add old data to diff string */
		for(i=0;i<ctrl[0];i++)
			if((oldpos+i>=0) && (oldpos+i<oldsize))
				newbuf[newpos+i]+=oldbuf[oldpos+i];

		/* Adjust pointers */
		newpos+=ctrl[0];
		oldpos+=ctrl[0];

		/* Sanity-check */
		if(newpos+ctrl[1]>newsize) {
			fprintf(stderr, "5: Corrupt diff data\n");
			rv = 0;
			goto out;
		}

		/* Read extra string */
		lenread = BUFREAD(&epf, newbuf + newpos, ctrl[1]);
		if (lenread < ctrl[1]) {
			fprintf(stderr, "6: Corrupt diff data\n");
			rv = 0;
			goto out;
		}

		/* Adjust pointers */
		newpos+=ctrl[1];
		oldpos+=ctrl[2];
	};

out:
	if (lzctrllen < ctrllen)
		slab_free(NULL, ctrldata);
	slab_free(NULL, diffdata);
	slab_free(NULL, extradata);

	return (rv);
}
