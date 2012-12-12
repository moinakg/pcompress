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
 */

#include "transpose.h"

/*
 * Perform a simple matrix transpose of the given buffer in "from".
 * If the buffer contains tables of numbers or structured data a
 * transpose can potentially help improve compression ratio by
 * bringing repeating values in columns into row ordering.
 */
void
transpose(unsigned char *from, unsigned char *to, uint64_t buflen, uint64_t stride, rowcol_t rc)
{
	uint64_t rows, cols, i, j, k, l;

	if (rc == ROW) {
		rows = buflen / stride;
		cols = stride;
	} else {
		cols = buflen / stride;
		rows = stride;
	}
	k = 0;
	for (j = 0; j < rows; j++) {
		l = 0;
		for (i = 0; i < cols; i++) {
			to[j + l] = from[i + k];
			l += rows;
		}
		k += cols;
	}
}
