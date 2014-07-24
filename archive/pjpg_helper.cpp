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
 */
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <utils.h>

#ifndef _MPLV2_LICENSE_
#include <packjpglib.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef unsigned char uchar_t;

/* a short reminder about input/output stream types
   for the pjglib_init_streams() function
	
	if input is file
	----------------
	in_scr -> name of input file
	in_type -> 0
	in_size -> ignore
	
	if input is memory
	------------------
	in_scr -> array containg data
	in_type -> 1
	in_size -> size of data array
	
	if input is *FILE (f.e. stdin)
	------------------------------
	in_src -> stream pointer
	in_type -> 2
	in_size -> ignore
	
	vice versa for output streams! */

#define	POLAROID_LE 0x64696f72616c6f50

/*
 * Helper routine to bridge to packJPG C++ lib, without changing packJPG itself.
 */
size_t
packjpg_filter_process(uchar_t *in_buf, size_t len, uchar_t **out_buf)
{
	unsigned int len1;
	uchar_t *pos;

	/*
	 * Workaround for packJPG limitation, not a bug per se. Images created with
	 * Polaroid cameras appear to have some weird huffman data in the middle which
	 * appears not to be interpreted by any image viewer/editor. This data gets
	 * stripped by packJPG.
	 * So the restored images will be visually correct, but, will be smaller than the
	 * original. So we need to look at the Exif Manufacturer tag for 'Polaroid' and
	 * skip those images. This should be within the first 512 bytes of the
	 * file (really...?) so we do a simple buffer scan without trying to parse Exif
	 * data.
	 */
	pos = (uchar_t *)memchr(in_buf, 'P', 512);
	while (pos) {
		if (LE64(U64_P(pos)) == POLAROID_LE)
			return (0);
		pos++;
		pos = (uchar_t *)memchr(pos, 'P', 512);
	}
	pjglib_init_streams(in_buf, 1, len, *out_buf, 1);
	len1 = len;
	if (!pjglib_convert_stream2mem(out_buf, &len1, NULL))
		return (0);
	if (len1 == len)
		return (0);
	return (len1);
}

#ifdef	__cplusplus
}
#endif
#endif
