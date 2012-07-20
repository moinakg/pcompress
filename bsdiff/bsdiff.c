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
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *
 * This is a somewhat modified bsdiff implementation. It has been modified
 * to do buffer to buffer diffing instead of file to file and also use
 * a custom RLE encoding rather than Bzip2 on the diff output.
 */

#if 0
__FBSDID("$FreeBSD: src/usr.bin/bsdiff/bsdiff/bsdiff.c,v 1.1 2005/08/06 01:59:05 cperciva Exp $");
#endif

#include <sys/types.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <allocator.h>
#include <utils.h>
#include "bscommon.h"

#define MIN(x,y) (((x)<(y)) ? (x) : (y))

static void split(bsize_t *I,bsize_t *V,bsize_t start,bsize_t len,bsize_t h)
{
	bsize_t i,j,k,x,tmp,jj,kk;

	if(len<16) {
		for(k=start;k<start+len;k+=j) {
			j=1;x=V[I[k]+h];
			for(i=1;k+i<start+len;i++) {
				if(V[I[k+i]+h]<x) {
					x=V[I[k+i]+h];
					j=0;
				};
				if(V[I[k+i]+h]==x) {
					tmp=I[k+j];I[k+j]=I[k+i];I[k+i]=tmp;
					j++;
				};
			};
			for(i=0;i<j;i++) V[I[k+i]]=k+j-1;
			if(j==1) I[k]=-1;
		};
		return;
	};

	x=V[I[start+len/2]+h];
	jj=0;kk=0;
	for(i=start;i<start+len;i++) {
		if(V[I[i]+h]<x) jj++;
		if(V[I[i]+h]==x) kk++;
	};
	jj+=start;kk+=jj;

	i=start;j=0;k=0;
	while(i<jj) {
		if(V[I[i]+h]<x) {
			i++;
		} else if(V[I[i]+h]==x) {
			tmp=I[i];I[i]=I[jj+j];I[jj+j]=tmp;
			j++;
		} else {
			tmp=I[i];I[i]=I[kk+k];I[kk+k]=tmp;
			k++;
		};
	};

	while(jj+j<kk) {
		if(V[I[jj+j]+h]==x) {
			j++;
		} else {
			tmp=I[jj+j];I[jj+j]=I[kk+k];I[kk+k]=tmp;
			k++;
		};
	};

	if(jj>start) split(I,V,start,jj-start,h);

	for(i=0;i<kk-jj;i++) V[I[jj+i]]=kk-1;
	if(jj==kk-1) I[jj]=-1;

	if(start+len>kk) split(I,V,kk,start+len-kk,h);
}

static void qsufsort(bsize_t *I,bsize_t *V,u_char *old,bsize_t oldsize)
{
	bsize_t buckets[256];
	bsize_t i,h,len;

	for(i=0;i<256;i++) buckets[i]=0;
	for(i=0;i<oldsize;i++) buckets[old[i]]++;
	for(i=1;i<256;i++) buckets[i]+=buckets[i-1];
	for(i=255;i>0;i--) buckets[i]=buckets[i-1];
	buckets[0]=0;

	for(i=0;i<oldsize;i++) I[++buckets[old[i]]]=i;
	I[0]=oldsize;
	for(i=0;i<oldsize;i++) V[i]=buckets[old[i]];
	V[oldsize]=0;
	for(i=1;i<256;i++) if(buckets[i]==buckets[i-1]+1) I[buckets[i]]=-1;
	I[0]=-1;

	for(h=1;I[0]!=-(oldsize+1);h+=h) {
		len=0;
		for(i=0;i<oldsize+1;) {
			if(I[i]<0) {
				len-=I[i];
				i-=I[i];
			} else {
				if(len) I[i-len]=-len;
				len=V[I[i]]+1-i;
				split(I,V,i,len,h);
				i+=len;
				len=0;
			};
		};
		if(len) I[i-len]=-len;
	};

	for(i=0;i<oldsize+1;i++) I[V[i]]=i;
}

static bsize_t matchlen(u_char *old,bsize_t oldsize,u_char *new,bsize_t newsize)
{
	bsize_t i;

	for(i=0;(i<oldsize)&&(i<newsize);i++)
		if(old[i]!=new[i]) break;

	return i;
}

static bsize_t search(bsize_t *I,u_char *old,bsize_t oldsize,
		u_char *new,bsize_t newsize,bsize_t st,bsize_t en,bsize_t *pos)
{
	bsize_t x,y;

	if(en-st<2) {
		x=matchlen(old+I[st],oldsize-I[st],new,newsize);
		y=matchlen(old+I[en],oldsize-I[en],new,newsize);

		if(x>y) {
			*pos=I[st];
			return x;
		} else {
			*pos=I[en];
			return y;
		}
	};

	x=st+(en-st)/2;
	if(memcmp(old+I[x],new,MIN(oldsize-I[x],newsize))<0) {
		return search(I,old,oldsize,new,newsize,x,en,pos);
	} else {
		return search(I,old,oldsize,new,newsize,st,x,pos);
	};
}

static void
valout(bsize_t x, u_char *buf)
{
	*((bsize_t *)buf) = htonll(x);
}

static void
valouti32(bsize_t x, u_char *buf)
{
	int32_t val;
	val = x;
	*((int32_t *)buf) = htonl(val);
}

bsize_t
bsdiff(u_char *old, bsize_t oldsize, u_char *new, bsize_t newsize,
       u_char *diff, u_char *scratch, bsize_t scratchsize)
{
	bsize_t *I,*V;
	bsize_t scan,pos,len;
	bsize_t lastscan,lastpos,lastoffset;
	bsize_t oldscore,scsc;
	bsize_t s,Sf,lenf,Sb,lenb;
	bsize_t overlap,Ss,lens;
	bsize_t i, rv;
	bsize_t dblen,eblen;
	u_char *db,*eb, *cb;
	u_char buf[sizeof (bsize_t)];
	u_char header[48];
	unsigned int sz, hdrsz, ulen;
	bufio_t pf;

	sz = sizeof (bsize_t);
	I = slab_alloc(NULL, (oldsize+1)*sz);
	V = slab_alloc(NULL, (oldsize+1)*sz);
	if(I == NULL || V == NULL) return (0);

	qsufsort(I,V,old,oldsize);
	slab_free(NULL, V);

	if(((db=slab_alloc(NULL, newsize+1))==NULL) ||
		((eb=slab_alloc(NULL, newsize+1))==NULL)) {
		fprintf(stderr, "bsdiff: Memory allocation error.\n");
		slab_free(NULL, I);
		slab_free(NULL, V);
		return (0);
	}
	dblen=0;
	eblen=0;
	BUFOPEN(&pf, diff, newsize);

	/* Header is
		0	4	compressed length of ctrl block
		4	4	actual length of ctrl block
		8	4	compressed length of diff block
		12	4	actual length of diff block
		16	4	compressed length of extra block
		20	4	actual length of extra block
		24	4	length of new file */
	/* File is
		0	28	Header
		28	??	ctrl block
		??	??	diff block
		??	??	extra block */
	valouti32(0, header);
	valouti32(0, header + 4);
	valouti32(0, header + 4*2);
	valouti32(0, header + 4*3);
	valouti32(0, header + 4*4);
	valouti32(0, header + 4*5);
	valouti32(newsize, header + 4*6);
	if (BUFWRITE(&pf, header, 4*7) != 4*7) {
		fprintf(stderr, "bsdiff: Write to compressed buffer failed.\n");
		rv = 0;
		goto out;
	}
	hdrsz = 4*7;

	/* Compute the differences, writing ctrl as we go */
	scan=0;len=0;
	lastscan=0;lastpos=0;lastoffset=0;
	while(scan<newsize) {
		oldscore=0;

		for(scsc=scan+=len;scan<newsize;scan++) {
			len=search(I,old,oldsize,new+scan,newsize-scan,
					0,oldsize,&pos);

			for(;scsc<scan+len;scsc++)
			if((scsc+lastoffset<oldsize) &&
				(old[scsc+lastoffset] == new[scsc]))
				oldscore++;

			if(((len==oldscore) && (len!=0)) || 
				(len>oldscore+sz)) break;

			if((scan+lastoffset<oldsize) &&
				(old[scan+lastoffset] == new[scan]))
				oldscore--;
		};

		if((len!=oldscore) || (scan==newsize)) {
			s=0;Sf=0;lenf=0;
			for(i=0;(lastscan+i<scan)&&(lastpos+i<oldsize);) {
				if(old[lastpos+i]==new[lastscan+i]) s++;
				i++;
				if(s*2-i>Sf*2-lenf) { Sf=s; lenf=i; };
			};

			lenb=0;
			if(scan<newsize) {
				s=0;Sb=0;
				for(i=1;(scan>=lastscan+i)&&(pos>=i);i++) {
					if(old[pos-i]==new[scan-i]) s++;
					if(s*2-i>Sb*2-lenb) { Sb=s; lenb=i; };
				};
			};

			if(lastscan+lenf>scan-lenb) {
				overlap=(lastscan+lenf)-(scan-lenb);
				s=0;Ss=0;lens=0;
				for(i=0;i<overlap;i++) {
					if(new[lastscan+lenf-overlap+i]==
					   old[lastpos+lenf-overlap+i]) s++;
					if(new[scan-lenb+i]==
					   old[pos-lenb+i]) s--;
					if(s>Ss) { Ss=s; lens=i+1; };
				};

				lenf+=lens-overlap;
				lenb-=lens;
			};

			for(i=0;i<lenf;i++)
				db[dblen+i]=new[lastscan+i]-old[lastpos+i];
			for(i=0;i<(scan-lenb)-(lastscan+lenf);i++)
				eb[eblen+i]=new[lastscan+lenf+i];

			dblen+=lenf;
			eblen+=(scan-lenb)-(lastscan+lenf);

			valouti32(lenf, buf);
			BUFWRITE(&pf, buf, 4);
			valouti32((scan-lenb)-(lastscan+lenf),buf);
			BUFWRITE(&pf, buf, 4);
			valouti32((pos-lenb)-(lastpos+lenf),buf);
			BUFWRITE(&pf, buf, 4);

			lastscan=scan-lenb;
			lastpos=pos-lenb;
			lastoffset=pos-scan;
		}
	}
	if (eblen > newsize/2) {
		rv = 0;
		goto out;
	}

	/* Comput uncompressed size of the ctrl data. */
	len = BUFTELL(&pf);
	valouti32(len-hdrsz, header+4);
	ulen = len-hdrsz;

	/* If our data can fit in the scratch area use it other alloc. */
	if (ulen > scratchsize) {
		cb = slab_alloc(NULL, ulen);
	} else {
		cb = scratch;
	}

	/*
	 * Attempt to RLE the ctrl data. If RLE succeeds and produces a smaller
	 * data then retain it.
	 */
	BUFSEEK(&pf, hdrsz, SEEK_SET);
	rv = zero_rle_encode(BUFPTR(&pf), ulen, cb, &ulen);
	if (rv == 0 && ulen < len-hdrsz) {
		BUFWRITE(&pf, cb, ulen);
	} else {
		BUFSEEK(&pf, len, SEEK_SET);
	}
	if (len-hdrsz > scratchsize) {
		slab_free(NULL, cb);
	}

	/* Compute compressed size of ctrl data */
	len = BUFTELL(&pf);
	valouti32(len-hdrsz, header);
	rv = len;

	/* Write diff data */
	len = newsize - rv;
	ulen = len;
	if (zero_rle_encode(db, dblen, BUFPTR(&pf), &ulen) == -1) {
		rv = 0;
		goto out;
	}
	/* Output size of diff data */
	len = ulen;
	valouti32(len, header + 4*2);
	valouti32(dblen, header + 4*3);
	rv += len;
	BUFSEEK(&pf, len, SEEK_CUR);

	/* Write extra data */
	len = newsize - rv;
	ulen = len;
	if (zero_rle_encode(eb, eblen, BUFPTR(&pf), &ulen) == -1) {
		rv = 0;
		goto out;
	}
	/* Output size of extra data */
	len = ulen;
	valouti32(len, header + 4*4);
	valouti32(eblen, header + 4*5);
	rv += len;

	/* Seek to the beginning, re-write the header.*/
	BUFSEEK(&pf, 0, SEEK_SET);
	BUFWRITE(&pf, header, hdrsz);

out:
	/* Free the memory we used */
	slab_free(NULL, db);
	slab_free(NULL, eb);
	slab_free(NULL, I);

	return (rv);
}
