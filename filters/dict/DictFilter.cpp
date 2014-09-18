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
 * moinakg@gmail.com, http://moinakg.wordpress.com/
 */

/*
 * Dict filter for text files. Adapted from Public Domain sources
 * of Fu Siyuan's CSC 3.2 archiver.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "DictFilter.h"
#include "Common.h"

const u32 wordNum = 123;

u8 wordList[wordNum][8] =
{
	"",
	"ac","ad","ai","al","am",
	"an","ar","as","at","ea",
	"ec","ed","ee","el","en",
	"er","es","et","id","ie",
	"ig","il","in","io","is",
	"it","of","ol","on","oo",
	"or","os","ou","ow","ul",
	"un","ur","us","ba","be",
	"ca","ce","co","ch","de",
	"di","ge","gh","ha","he",
	"hi","ho","ra","re","ri",
	"ro","rs","la","le","li",
	"lo","ld","ll","ly","se",
	"si","so","sh","ss","st",
	"ma","me","mi","ne","nc",
	"nd","ng","nt","pa","pe",
	"ta","te","ti","to","th",
	"tr","wa","ve",
	"all","and","but","dow",
	"for","had","hav","her",
	"him","his","man","mor",
	"not","now","one","out",
	"she","the","was","wer",
	"whi","whe","wit","you",
	"any","are",
	"that","said","with","have",
	"this","from","were","tion",
};


void
DictFilter::MakeWordTree()
{
	u32 i,j;
	u32 treePos;
	u8 symbolIndex = 0x82;

	nodeMum = 1;

	memset(wordTree,0,sizeof(wordTree));

	for (i = 1; i < wordNum; i++) {
		treePos = 0;
		for(j = 0; wordList[i][j] != 0; j++) {
			u32 idx = wordList[i][j] - 'a';
			if (wordTree[treePos].next[idx]) {
				treePos = wordTree[treePos].next[idx];
			} else {
				wordTree[treePos].next[idx] = nodeMum;
				treePos = nodeMum;
				nodeMum++;
			}
		}
		wordIndex[symbolIndex] = i;
		wordTree[treePos].symbol = symbolIndex++;
	}

	maxSymbol=symbolIndex;

}


DictFilter::DictFilter()
{
	MakeWordTree();
}



DictFilter::~DictFilter()
{
}


u32
DictFilter::Forward_Dict(u8 *src, u32 size, u8 *dst, u32 *dstsize)
{
	if (size < 16384)
		return 0;

	u32 i,j,treePos = 0;
	u32 lastSymbol = 0;
	u32 dstSize = 0;
	u32 idx;


	for(i = 0; i < size-5;) {
		if (src[i] >= 'a' && src[i] <= 'z') {

			u32 matchSymbol = 0,longestWord = 0;
			treePos = 0;
			for(j = 0;;) {
				idx = src[i+j] - 'a';
				if (idx < 0 || idx > 25)
					break;
				if (wordTree[treePos].next[idx] == 0)
					break;

				treePos=wordTree[treePos].next[idx];
				j++;
				if (wordTree[treePos].symbol) {
					matchSymbol = wordTree[treePos].symbol;
					longestWord = j;
				}
			}

			if (matchSymbol) {
				dst[dstSize++] = matchSymbol;
				i += longestWord;
				continue;
			}
			lastSymbol = 0;
			dst[dstSize++] = src[i];
			i++;
		} else {
			if (src[i] >= 0x82) {
				dst[dstSize++] = 254;
				dst[dstSize++] = src[i];
			}
			else
				dst[dstSize++] = src[i];

			lastSymbol = 0;
			treePos = 0;
			i++;
		}

	}

	for (; i<size; i++) {
		if (src[i] >= 0x82) {
			dst[dstSize++] = 254;
			dst[dstSize++] = src[i];
		}
		else
			dst[dstSize++] = src[i];
	}

	if (dstSize > size*0.82)
		return 0;

	*dstsize = dstSize;
	return 1;
}

void
DictFilter::Inverse_Dict(u8 *src, u32 size, u8 *dst, u32 *dstsize)
{

	u32 i = 0,j;
	u32 dstPos = 0,idx;

	while(dstPos < *dstsize && i < size) {
		if (src[i] >= 0x82 && src[i] < maxSymbol) {
			idx = wordIndex[src[i]];
			for(j=0; wordList[idx][j]; j++)
				dst[dstPos++] = wordList[idx][j];
		}
		else if (src[i] == 254 && (i+1 < size && src[i+1] >= 0x82)) {
			i++;
			dst[dstPos++] = src[i];
		}
		else {
			dst[dstPos++] = src[i];
		}

		i++;
	}
	*dstsize = dstPos;
}

#ifdef  __cplusplus
extern "C" {
#endif

void *
new_dict_context()
{
	DictFilter *df = new DictFilter();
	return (static_cast<void *>(df));
}

void
delete_dict_context(void *dict_ctx)
{
	if (dict_ctx) {
		DictFilter *df = static_cast<DictFilter *>(dict_ctx);
		delete df;
	}
}

int
dict_encode(void *dict_ctx, uchar_t *from, uint64_t fromlen, uchar_t *to, uint64_t *dstlen)
{
	DictFilter *df = static_cast<DictFilter *>(dict_ctx);
	u32 fl = fromlen;
	u32 dl = *dstlen;
	u8 *dst;

	if (fromlen > UINT32_MAX)
		return (-1);
	U32_P(to) = LE32(fromlen);
	dst = to + 4;
	dl -= 4;
	if (df->Forward_Dict(from, fl, dst, &dl)) {
		*dstlen = dl + 4;
		return (0);
	}
	return (-1);
}

int
dict_decode(void *dict_ctx, uchar_t *from, uint64_t fromlen, uchar_t *to, uint64_t *dstlen)
{
	DictFilter *df = static_cast<DictFilter *>(dict_ctx);
	u32 fl = fromlen;
	u32 dl;
	u8 *src;

	dl = U32_P(from);
	if (dl > *dstlen) {
		log_msg(LOG_ERR, 0, "Destination overflow in dict_decode.");
		return (-1);
	}
	*dstlen = dl;
	src = from + 4;
	fl -= 4;

	df->Inverse_Dict(src, fl, to, &dl);
	if (dl < *dstlen)
		return (-1);
	return (0);
}

#ifdef  __cplusplus
}
#endif
