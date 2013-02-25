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

#include <stdio.h>
#include <utils.h>
#include <cpuid.h>

int
main(void)
{
	processor_info_t pc;
	cpuid_basic_identify(&pc);
	if (pc.sse_level == 3 && pc.sse_sub_level == 1) {
		printf("ssse%d", pc.sse_level);
		pc.sse_sub_level = 0;
	} else {
		printf("sse%d", pc.sse_level);
	}
	if (pc.sse_sub_level > 0)
		printf(".%d\n", pc.sse_sub_level);
	else
		printf("\n");
	return (0);
}

