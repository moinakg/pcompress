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

#ifndef __ALLOCATOR_H__
#define __ALLOCATOR_H__

#include <sys/types.h>
#include <inttypes.h>

void slab_init();
void slab_cleanup(int quiet);
void *slab_alloc(void *p, uint64_t size);
void *slab_calloc(void *p, uint64_t items, uint64_t size);
void slab_free(void *p, void *address);
void slab_release(void *p, void *address);
int slab_cache_add(uint64_t size);

#endif

