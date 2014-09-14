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
#ifdef _ENABLE_WAVPACK_

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <utils.h>
#include "wavpack.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct write_data_s {
	uint32_t bytes_written, first_block_size;
	uint32_t bufsize;
	uchar_t *buf;
	int error;
} write_data;

typedef struct read_data_s {
	uint32_t bytes_read;
	uint32_t bufsize;
	uchar_t *buf;
} read_data;

#define	TRUE	1
#define	FALSE	0

/*
 * Utility functions to read and write to memory areas as if they are files.
 */
static int
write_block (void *id, void *data, int32_t length)
{
	write_data *wid = (write_data *) id;

	if (wid->error)
		return FALSE;

	if (wid && wid->buf && data && length) {
		if (wid->bytes_written + length <= wid->bufsize) {
			memcpy(wid->buf + wid->bytes_written, data, length);
			wid->bytes_written += length;

			if (!wid->first_block_size)
				wid->first_block_size = length;
		} else {
			wid->error = 1;
			return FALSE;
		}
	}
	return TRUE;
}

static int
read_block(read_data *rdat, uchar_t *tgt, uint32_t len, uint32_t *numread)
{
	uint32_t numcopy;

	if (rdat->bufsize - rdat->bytes_read >= len)
		numcopy = len;
	else
		numcopy = rdat->bufsize - rdat->bytes_read;

	if (numcopy == 0)
		return (1);
	memcpy(tgt, rdat->buf + rdat->bytes_read, numcopy);
	rdat->bytes_read += numcopy;
	*numread = numcopy;
	return (0);
}

/*
 * This function returns a direct pointer into the data area rather than
 * copying the data into another supplied buffer. This is faster than
 * the traditional read semantics as in read_block() above.
 */
static int
read_block_ref(read_data *rdat, uchar_t **ref, uint32_t len, uint32_t *numread)
{
	uint32_t numcopy;

	if (rdat->bufsize - rdat->bytes_read >= len)
		numcopy = len;
	else
		numcopy = rdat->bufsize - rdat->bytes_read;

	if (numcopy == 0)
		return (1);
	*ref = rdat->buf + rdat->bytes_read;
	rdat->bytes_read += numcopy;
	*numread = numcopy;
	return (0);
}

#define INPUT_SAMPLES 65536

static int
pack_audio(WavpackContext *wpc, read_data *rdat)
{
	uint32_t samples_remaining, input_samples = INPUT_SAMPLES, samples_read = 0;
	int bytes_per_sample;
	int32_t *sample_buffer;
	unsigned char *input_buffer;

	// don't use an absurd amount of memory just because we have an absurd number of channels

	while (input_samples * sizeof (int32_t) * WavpackGetNumChannels (wpc) > 2048*1024)
		input_samples >>= 1;

	WavpackPackInit(wpc);
	bytes_per_sample = WavpackGetBytesPerSample (wpc) * WavpackGetNumChannels (wpc);
	//input_buffer = malloc(input_samples * bytes_per_sample);
	sample_buffer = malloc(input_samples * sizeof (int32_t) * WavpackGetNumChannels (wpc));
	samples_remaining = WavpackGetNumSamples (wpc);

	while (1) {
		uint32_t bytes_to_read, bytes_read = 0;
		unsigned int sample_count;

		if (samples_remaining > input_samples)
			bytes_to_read = input_samples * bytes_per_sample;
		else
			bytes_to_read = samples_remaining * bytes_per_sample;

		samples_remaining -= bytes_to_read / bytes_per_sample;
		read_block_ref(rdat, &input_buffer, bytes_to_read, &bytes_read);
		samples_read += sample_count = bytes_read / bytes_per_sample;

		if (!sample_count) {
			break;
		} else {
			unsigned int cnt = sample_count * WavpackGetNumChannels(wpc);
			unsigned char *sptr = input_buffer;
			int32_t *dptr = sample_buffer;

			switch (WavpackGetBytesPerSample (wpc)) {
			    case 1:
				while (cnt--)
					*dptr++ = *sptr++ - 128;
				break;

			    case 2:
				while (cnt--) {
					*dptr++ = sptr [0] | ((int32_t)(signed char) sptr [1] << 8);
					sptr += 2;
				}
				break;

			    case 3:
				while (cnt--) {
					*dptr++ = sptr [0] | ((int32_t) sptr [1] << 8) |
					    ((int32_t)(signed char) sptr [2] << 16);
					sptr += 3;
				}
				break;

			    case 4:
				while (cnt--) {
					*dptr++ = sptr [0] | ((int32_t) sptr [1] << 8) |
					    ((int32_t) sptr [2] << 16) |
					    ((int32_t)(signed char) sptr [3] << 24);
					sptr += 4;
				}
				break;
			}
		}

		if (!WavpackPackSamples(wpc, sample_buffer, sample_count)) {
			free(sample_buffer);
			return (0);
		}
	}

	free(sample_buffer);
	if (!WavpackFlushSamples (wpc)) {
		return (0);
	}

	return (1);
}

/*
 * Helper routine for wavpack. Higher level encoding interface adapted from
 * pack_file() in cli/wavpack.c
 */
size_t
wavpack_filter_encode(uchar_t *in_buf, size_t len, uchar_t **out_buf)
{
	uint32_t total_samples = 0, bcount;
	WavpackConfig loc_config;
	RiffChunkHeader riff_chunk_header;
	write_data wv_dat;
	read_data  rd_dat;
	ChunkHeader chunk_header;
	WaveHeader WaveHeader;
	WavpackContext *wpc;
	int adobe_mode, result;

	memset(&WaveHeader, 0, sizeof (WaveHeader));
	memset(&wv_dat, 0, sizeof (wv_dat));
	memset(&rd_dat, 0, sizeof (rd_dat));
	memset(&loc_config, 0, sizeof (loc_config));
	adobe_mode = 0;

	/*
	 * Default WavPack config.
	 */
	loc_config.flags |= CONFIG_FAST_FLAG;

	*out_buf = (uchar_t *)malloc(len);
	if (*out_buf == NULL)
		return (0);

	wv_dat.buf = *out_buf;
	wv_dat.bufsize = len;
	wpc = WavpackOpenFileOutput (write_block, &wv_dat, NULL);

	rd_dat.buf = in_buf;
	rd_dat.bufsize = len;

	// read (and copy to output) initial RIFF form header
	if (read_block(&rd_dat, (uchar_t *)&riff_chunk_header, sizeof (RiffChunkHeader), &bcount) ||
	    bcount != sizeof (RiffChunkHeader) || strncmp (riff_chunk_header.ckID, "RIFF", 4) ||
	    strncmp (riff_chunk_header.formType, "WAVE", 4)) {
		WavpackCloseFile (wpc);
		return (0);
	}

	// loop through all elements of the RIFF wav header
	// (until the data chunk) and copy them to the output
	//
	while (1) {
		if (read_block(&rd_dat, (uchar_t *)&chunk_header, sizeof (ChunkHeader), &bcount) ||
		    bcount != sizeof (ChunkHeader)) {
			WavpackCloseFile (wpc);
			return (0);
		}

		WavpackLittleEndianToNative (&chunk_header, ChunkHeaderFormat);

		// if it's the format chunk, we want to get some info out of there and
		// make sure it's a .wav file we can handle
		//
		if (strncmp(chunk_header.ckID, "fmt ", 4) == 0) {
			int supported = TRUE, format;

			if (chunk_header.ckSize < 16 || chunk_header.ckSize > sizeof (WaveHeader) ||
			    !read_block(&rd_dat, (uchar_t *)&WaveHeader, chunk_header.ckSize, &bcount) ||
			    bcount != chunk_header.ckSize) {
				WavpackCloseFile (wpc);
				return (0);
			}
			WavpackLittleEndianToNative (&WaveHeader, WaveHeaderFormat);

			if (chunk_header.ckSize > 16 && WaveHeader.cbSize == 2)
				adobe_mode = 1;

			format = (WaveHeader.FormatTag == 0xfffe && chunk_header.ckSize == 40) ?
			    WaveHeader.SubFormat : WaveHeader.FormatTag;
			loc_config.bits_per_sample = (chunk_header.ckSize == 40 &&
			    WaveHeader.ValidBitsPerSample) ?
			    WaveHeader.ValidBitsPerSample : WaveHeader.BitsPerSample;

			if (format != 1 && format != 3)
				supported = FALSE;
			if (format == 3 && loc_config.bits_per_sample != 32)
				supported = FALSE;
			if (!WaveHeader.NumChannels || WaveHeader.NumChannels > 256 ||
			    WaveHeader.BlockAlign / WaveHeader.NumChannels < 
			    (loc_config.bits_per_sample + 7) / 8 ||
			    WaveHeader.BlockAlign / WaveHeader.NumChannels > 4 ||
			    WaveHeader.BlockAlign % WaveHeader.NumChannels)
				supported = FALSE;
			if (loc_config.bits_per_sample < 1 || loc_config.bits_per_sample > 32)
				supported = FALSE;

			if (!supported) {
				WavpackCloseFile (wpc);
				return (0);
			}

			if (chunk_header.ckSize < 40) {
				if (WaveHeader.NumChannels <= 2)
					loc_config.channel_mask = 0x5 - WaveHeader.NumChannels;
				else if (WaveHeader.NumChannels <= 18)
					loc_config.channel_mask = (1 << WaveHeader.NumChannels) - 1;
				else
					loc_config.channel_mask = 0x3ffff;
			} else {
				loc_config.channel_mask = WaveHeader.ChannelMask;
			}
			if (format == 3) {
				loc_config.float_norm_exp = 127;
			} else if (adobe_mode &&
			    WaveHeader.BlockAlign / WaveHeader.NumChannels == 4) {
				if (WaveHeader.BitsPerSample == 24)
					loc_config.float_norm_exp = 127 + 23;
				else if (WaveHeader.BitsPerSample == 32)
					loc_config.float_norm_exp = 127 + 15;
			}
		} else if (strncmp(chunk_header.ckID, "data", 4) == 0) {

			// on the data chunk, get size and exit loop
			if (!WaveHeader.NumChannels) {      // make sure we saw a "fmt" chunk...
				WavpackCloseFile(wpc);
				return (0);
			}

			if (len && len - chunk_header.ckSize > 16777216) {
				WavpackCloseFile(wpc);
				return (0);
			}
			total_samples = chunk_header.ckSize / WaveHeader.BlockAlign;

			if (!total_samples) {
				WavpackCloseFile(wpc);
				return (0);
			}

			loc_config.bytes_per_sample = WaveHeader.BlockAlign / WaveHeader.NumChannels;
			loc_config.num_channels = WaveHeader.NumChannels;
			loc_config.sample_rate = WaveHeader.SampleRate;
			break;
		} else {     // just copy unknown chunks to output
			int bytes_to_copy = (chunk_header.ckSize + 1) & ~1L;
			uchar_t *rbuf;

			rbuf = NULL; // Silence compiler
			if (!read_block_ref(&rd_dat, &rbuf, bytes_to_copy, &bcount) ||
			    bcount != bytes_to_copy ||
			    !WavpackAddWrapper(wpc, rbuf, bytes_to_copy)) {
				WavpackCloseFile(wpc);
				return (0);
			}
		}
	}

	if (!WavpackSetConfiguration(wpc, &loc_config, total_samples)) {
		WavpackCloseFile (wpc);
		return (0);
	}

	// pack the audio portion of the data now;
	result = pack_audio(wpc, &rd_dat);

	/*
	 * if everything went well (and we're not ignoring length) try to read
	 * anything else that might be appended to the audio data and write that
	 * to the WavPack metadata as "wrapper"
	 */
	if (result) {
		uchar_t *buff;

		buff = NULL; // Silence compiler
		while (read_block_ref(&rd_dat, &buff, 16, &bcount) && bcount) {
			if (!WavpackAddWrapper (wpc, buff, bcount)) {
				WavpackCloseFile(wpc);
				return (0);
			}
		}
	}

	// we're now done with any WavPack blocks, so flush any remaining data
	if (result && !WavpackFlushSamples (wpc)) {
		WavpackCloseFile(wpc);
		return (0);
	}

	WavpackCloseFile(wpc);
	return (wv_dat.bytes_written);
}

#ifdef	__cplusplus
}
#endif

#endif /* _ENABLE_WAVPACK_ */
