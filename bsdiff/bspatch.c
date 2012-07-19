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
#include "bscommon.h"

static bsize_t
valin(u_char *buf)
{
	return ntohll(*((bsize_t *)buf));
}

static int32_t
valini32(u_char *buf)
{
	return ntohl(*((int32_t *)buf));
}

bsize_t
get_bsdiff_sz(u_char *pbuf) {
	bsize_t newsize;
	bsize_t ctrllen, lzdatalen, datalen, lzextralen, extralen;
	int sz, hdrsz, rv;

	sz = sizeof (bsize_t);
	hdrsz = sz*6;

	ctrllen = valin(pbuf);
	lzdatalen = valin(pbuf+sz);
	datalen = valin(pbuf+sz*2);
	lzextralen = valin(pbuf+sz*3);
	extralen = valin(pbuf+sz*4);
	newsize = valin(pbuf+sz*5);
	return (ctrllen + lzdatalen + lzextralen + hdrsz);
}

int
bspatch(u_char *pbuf, u_char *old, bsize_t oldsize, u_char *new, bsize_t *_newsize)
{
	bsize_t newsize;
	bsize_t ctrllen, lzdatalen, datalen, lzextralen, extralen;
	u_char buf[8];
	u_char *diffdata, *extradata;
	bsize_t oldpos,newpos;
	bsize_t ctrl[3];
	bsize_t lenread;
	bsize_t i;
	bufio_t cpf, dpf, epf;
	int sz, hdrsz, rv;
	unsigned int len;

	/*
	File format:
		0	8	length of ctrl block (X)
		8	8	compressed length of diff block (Y)
		16	8	actual length of diff block
		24	8	compressed length of extra block (Z)
		32	8	actual length of extra block
		40	8	length of new file
		48	X	control block
		48+X	Y	lzfx(diff block)
		48+X+Y	Z	lzfx(extra block)
	with control block a set of triples (x,y,z) meaning "add x bytes
	from oldfile to x bytes from the diff block; copy y bytes from the
	extra block; seek forwards in oldfile by z bytes".
	*/
	sz = sizeof (bsize_t);
	hdrsz = sz*6;
	rv = 1;

	/* Read lengths from header first. */
	ctrllen = valin(pbuf);
	lzdatalen = valin(pbuf+sz);
	datalen = valin(pbuf+sz*2);
	lzextralen = valin(pbuf+sz*3);
	extralen = valin(pbuf+sz*4);
	newsize = valin(pbuf+sz*5);

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
	diffdata = malloc(datalen);
	extradata = malloc(extralen);
	if (diffdata == NULL || extradata == NULL) {
		fprintf(stderr, "bspatch: Out of memory.\n");
		if (diffdata) free(diffdata);
		if (extradata) free(extradata);
		return (0);
	}

	/* Decompress diffdata and extradata. */
	len = datalen;
	if (zero_rle_decode(pbuf + hdrsz + ctrllen, lzdatalen, diffdata, &len) == -1 ||
	    len != datalen) {
		fprintf(stderr, "bspatch: Failed to decompress diff data.\n");
		rv = 0;
		goto out;
	}
	datalen = len;

	len = extralen;
	if (zero_rle_decode(pbuf + hdrsz + ctrllen + lzdatalen, lzextralen, extradata, &len) == -1 ||
	    len != extralen) {
		fprintf(stderr, "bspatch: Failed to decompress extra data.\n");
		rv = 0;
		goto out;
	}
	extralen = len;
	BUFOPEN(&cpf, pbuf + hdrsz, ctrllen);
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
		lenread = BUFREAD(&dpf, new + newpos, ctrl[0]);
		if (lenread < ctrl[0]) {
			fprintf(stderr, "4: Corrupt diff data\n");
			rv = 0;
			goto out;
		}

		/* Add old data to diff string */
		for(i=0;i<ctrl[0];i++)
			if((oldpos+i>=0) && (oldpos+i<oldsize))
				new[newpos+i]+=old[oldpos+i];

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
		lenread = BUFREAD(&epf, new + newpos, ctrl[1]);
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
	free(diffdata);
	free(extradata);

	return (rv);
}
