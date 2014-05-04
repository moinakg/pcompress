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
 */

#include <stdio.h>
#include <stdlib.h>
#include <utils.h>
#include <cpuid.h>

void
usage(void)
{
	printf("Usage: sse_level [--avx]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	processor_cap_t pc;
	int avx_detect = 0;
	cpuid_basic_identify(&pc);

	if (argc > 1) {
		if (strcmp(argv[1], "--avx") == 0)
			avx_detect = 1;
		else
			usage();
	}
	if (!avx_detect) {
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
	} else {
		if (pc.avx_level == 1)
			printf("avx\n");
		else if (pc.avx_level == 2)
			printf("avx2\n");
	}

	return (0);
}

