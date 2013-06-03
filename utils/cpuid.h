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

/*
 * Copyright 2008  Veselin Georgiev,
 * anrieffNOSPAM @ mgail_DOT.com (convert to gmail)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __CPUID_H__
#define __CPUID_H__

#ifdef	__x86_64__
#define VENDOR_STR_MAX          16
#define BRAND_STR_MAX           64
#define CPU_FLAGS_MAX           128
#define MAX_CPUID_LEVEL         32
#define MAX_EXT_CPUID_LEVEL     32
#define MAX_INTELFN4_LEVEL      4

typedef enum {
	PROC_BIGENDIAN_GENERIC = 1,
	PROC_LITENDIAN_GENERIC,
	PROC_X64_INTEL,
	PROC_X64_AMD
} proc_type_t;

typedef struct {
	int sse_level;
	int sse_sub_level;
	int avx_level;
	int xop_avail;
	int aes_avail;
	proc_type_t proc_type;
} processor_info_t;

/**
 * This contains only the most basic CPU data, required to do identification
 * and feature recognition. Every processor should be identifiable using this
 * data only.
 */
struct cpu_raw_data_t {
	/** contains results of CPUID for eax = 0, 1, ...*/
	uint32_t basic_cpuid[MAX_CPUID_LEVEL][4];

	/** contains results of CPUID for eax = 0x80000000, 0x80000001, ...*/
	uint32_t ext_cpuid[MAX_EXT_CPUID_LEVEL][4];

	/** when the CPU is intel and it supports deterministic cache
	    information: this contains the results of CPUID for eax = 4
	    and ecx = 0, 1, ... */
	uint32_t intel_fn4[MAX_INTELFN4_LEVEL][4];
	char vendor_str[VENDOR_STR_MAX];
};

void cpuid_get_raw_data(struct cpu_raw_data_t* data);
void cpuid_basic_identify(processor_info_t *pc);

#endif /* __x86_64__ */

#endif /* __CPUID_H__ */

