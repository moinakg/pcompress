/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2014 Moinak Ghosh. All rights reserved.
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
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "utils.h"
#include "winsupport.h"
#include "types.hpp"
#include "dis.hpp"

#ifdef	__cplusplus
extern "C" {
#endif

typedef unsigned char uchar_t;
#define DISPACK_MAGIC	"DisPack "

#pragma pack(1)
struct FileHeader
{
	char magic[8];
	sU32 SizeBefore;      // number of bytes before start of code section
	sU32 SizeAfter;       // number of bytes after code section
	sU32 SizeTransformed; // size of transformed code section
	sU32 SizeOriginal;    // size of untransformed code section
	sU32 Origin;          // virtual address of first byte
};
#pragma pack()

size_t
dispack_filter_encode(uchar_t *inData, size_t len, uchar_t **out_buf)
{
	uchar_t *pos;
	FileHeader hdr;
	sU32 sizeNow;

	*out_buf = (uchar_t *)malloc(len);
	if (*out_buf == NULL)
		return (0);

	// assume the input file is a PE executable.
	IMAGE_DOS_HEADER *doshdr = (IMAGE_DOS_HEADER *) inData;
	IMAGE_NT_HEADERS *nthdr = (IMAGE_NT_HEADERS *) (inData + doshdr->e_lfanew);

	if (nthdr->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
	    nthdr->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
		// Only 32-bit PE files for x86 supported
		return (0);
	}

	sU32 imageBase = nthdr->OptionalHeader.ImageBase;
	sU32 codeStart = nthdr->OptionalHeader.BaseOfCode;
	sU32 codeSize = nthdr->OptionalHeader.SizeOfCode;
	sU32 fileOffs = 0; // find file offset of first section

	// find section containing code
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nthdr);
	for (sInt i=0;i<nthdr->FileHeader.NumberOfSections;i++) {
		if (codeStart >= sec[i].VirtualAddress && codeStart <
		    sec[i].VirtualAddress + sec[i].SizeOfRawData)
			fileOffs = sec[i].PointerToRawData + (codeStart - sec[i].VirtualAddress);
	}

	if (fileOffs == 0) {
		// Code section not found!
		return (0);
	}

	// Keep space for header
	pos = *out_buf + sizeof (hdr);

	// transform code
	sU32 transSize = len - sizeof (hdr);
	if (fileOffs + codeSize > len) {
		codeSize = len - fileOffs;
	}
	if (DisFilter(inData + fileOffs, codeSize, imageBase + codeStart, pos, transSize) == NULL) {
		goto cmp_E89;
	}
	pos += transSize;

	// Give up if dispack savings is not enough for header space and we can overflow the buffer.
	memcpy(hdr.magic, DISPACK_MAGIC, strlen(DISPACK_MAGIC));
	hdr.SizeBefore = fileOffs;
	hdr.SizeAfter = len - (fileOffs + codeSize);
	hdr.SizeTransformed = transSize;
	hdr.SizeOriginal = codeSize;
	hdr.Origin = imageBase + codeStart;
	sizeNow = pos - *out_buf;
	if (sizeNow + hdr.SizeBefore + hdr.SizeAfter >= len) {
		goto cmp_E89;
	}

	// Now plonk the header
	memcpy(*out_buf, &hdr, sizeof (hdr));

	if (hdr.SizeBefore > 0) {
		// Copy rest of the data
		memcpy(pos, inData, hdr.SizeBefore);
		pos += hdr.SizeBefore;
	}
	if (hdr.SizeAfter > 0) {
		memcpy(pos, inData + (fileOffs + codeSize), hdr.SizeAfter);
		pos += hdr.SizeAfter;
	}

	return (pos -  *out_buf);
cmp_E89:
	// Apply an E8E9 filter this does not put the DISPACK_MAGIC into the
	// file header. So, when decoding, that is detected and E8E9 decode
	// is applied.
	memcpy(*out_buf, inData, len);
	if (Forward_E89(*out_buf, len) != 0)
		return (0);
	return (len);
}

size_t
dispack_filter_decode(uchar_t *inData, size_t len, uchar_t **out_buf)
{
	uchar_t *decoded;
	FileHeader *hdr = (FileHeader *)inData;

	sU8 *transformed = inData + sizeof (FileHeader);
	sU8 *before = transformed + hdr->SizeTransformed;
	sU8 *after = before + hdr->SizeBefore;

	// alloc buffer for unfiltered code
	*out_buf = (uchar_t *)malloc(len);
	if (*out_buf == NULL)
		return (0);

	if (memcmp(hdr->magic, DISPACK_MAGIC, strlen(DISPACK_MAGIC)) != 0)
		goto dec_E89;

	decoded = *out_buf;
	memcpy(decoded, before, hdr->SizeBefore);
	decoded += hdr->SizeBefore;

	if (!DisUnFilter(transformed, hdr->SizeTransformed, decoded,
	    hdr->SizeOriginal, hdr->Origin)) {
		return (0);
	}
	decoded += hdr->SizeOriginal;
	memcpy(decoded, after, hdr->SizeAfter);
	decoded += hdr->SizeAfter;

	return (decoded - *out_buf);
dec_E89:
	memcpy(*out_buf, inData, len);
	if (Inverse_E89(*out_buf, len) == -1)
		return (0);
	return (len);
}

#ifdef	__cplusplus
}
#endif
