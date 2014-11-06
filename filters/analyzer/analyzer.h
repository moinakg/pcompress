/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2014 Moinak Ghosh. All rights reserved.
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
 */

#ifndef	_ANALYZER_H
#define	_ANALYZER_H

#ifdef  __cplusplus
extern "C" {
#endif

struct significance_value {
	int btype;
};

typedef struct _analyzer_ctx {
	struct significance_value one_pct;
	struct significance_value forty_pct;
	struct significance_value fifty_pct;
} analyzer_ctx_t;

void analyze_buffer(void *src, uint64_t srclen, analyzer_ctx_t *actx);
int analyze_buffer_simple(void *src, uint64_t srclen);

#ifdef  __cplusplus
}
#endif

#endif
