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

/*
 * pcompress - Do a chunked parallel compression/decompression of a file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <libgen.h>
#include "pcompress.h"
#include <ctype.h>
#include <utils.h>

int
main(int argc, char *argv[])
{
	int err;
	pc_ctx_t *pctx;

	err = 0;
	pctx = create_pc_context();

	err = init_pc_context(pctx, argc, argv);
	if (err != 0 && err != 2) {
		log_msg(LOG_ERR, 0, "Invalid arguments to pcompress.\n");
		log_msg(LOG_ERR, 0, "Please see usage.\n");
		destroy_pc_context(pctx);
		return (err);

	} else if (err == 2) {
		usage(pctx);
		destroy_pc_context(pctx);
		return (0);
	}

	/*
	 * Start the main routines.
	 */
	err = start_pcompress(pctx);
	destroy_pc_context(pctx);
	return (err);
}
