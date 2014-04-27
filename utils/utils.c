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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#ifndef __APPLE__
#include <link.h>
#endif
#include <signal.h>
#include <rabin_dedup.h>
#include <cpuid.h>
#include <xxhash.h>
#include <pc_archive.h>

#ifndef __APPLE__
#include <sys/sysinfo.h>
#else
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_time.h>

static mach_timebase_info_data_t sTimebaseInfo;
#endif

#define _IN_UTILS_
#include "utils.h"

processor_cap_t proc_info;
pthread_mutex_t f_mutex = PTHREAD_MUTEX_INITIALIZER;
static int cur_log_level = 2;
static log_dest_t ldest = {LOG_OUTPUT, LOG_INFO, NULL};
static char *f_name_list[512];
static int f_count = 512, f_inited = 0;

void
init_pcompress() {
	cpuid_basic_identify(&proc_info);
	XXH32_module_init();
#ifdef __APPLE__
	(void) mach_timebase_info(&sTimebaseInfo);
#endif
}

/*
 * Fetch the command name that started the current process.
 * The returned string must be freed by the caller.
 */
const char *
get_execname(const char *argv0)
{
	char path[MAXPATHLEN];
	char apath[128];
	char *tmp1, *tmp2;
	pid_t pid;

	/* The easiest case: we are in linux */
	if (readlink("/proc/self/exe", path, MAXPATHLEN) != -1) {
		return (strdup(basename(path)));
	}

	/* Next easy case: Solaris/Illumos */
	pid = getpid();
	sprintf(apath, "/proc/%d/path/a.out", pid);
	if (readlink(apath, path, MAXPATHLEN) != -1) {
		return (strdup(basename(path)));
	}

	/* Oops... not in linux, not in Solaris no  guarantee */
	/* check if we have something like execve("foobar", NULL, NULL) */
	if (argv0 == NULL) {
		/* Give up */
		return (strdup("Unknown"));
	}

	tmp1 = strdup(argv0);
	tmp2 = strdup(basename(tmp1));
	free(tmp1);
	return (tmp2);
}

/*
 * Routines to parse a numeric string which can have the following suffixes:
 * k - Kilobyte
 * m - Megabyte
 * g - Gigabyte
 *
 * The number should fit in an int64_t data type.
 * Numeric overflow is also checked. The routine parse_numeric() returns
 * 1 if there was a numeric overflow.
 */
static int
raise_by_multiplier(int64_t *val, int mult, int power) {
	int64_t result;

	while (power-- > 0) {
		result = *val * mult;
		if (result/mult != *val)
			return (1);
		*val = result;
	}
	return (0);
}

int
parse_numeric(int64_t *val, const char *str)
{
	int ovr = 0;
	char *mult;

	*val = strtoll(str, &mult, 0);
	if (*mult != '\0') {
		switch (*mult) {
		  case 'k':
		  case 'K':
			ovr = raise_by_multiplier(val, 1024, 1);
			break;
		  case 'm':
		  case 'M':
			ovr = raise_by_multiplier(val, 1024, 2);
			break;
		  case 'g':
		  case 'G':
			ovr = raise_by_multiplier(val, 1024, 3);
			break;
		  default:
			ovr = 2;
		}
	}

	return (ovr);
}

/*
 * Convert number of bytes into human readable format
 */
char *
bytes_to_size(uint64_t bytes)
{
	static char num[20];
	uint64_t kilobyte = 1024;
	uint64_t megabyte = kilobyte * 1024;
	uint64_t gigabyte = megabyte * 1024;
	uint64_t terabyte = gigabyte * 1024;

	if (bytes < kilobyte) {
		sprintf(num, "%" PRIu64 " B", bytes);

	} else if (bytes < megabyte) {
		sprintf(num, "%" PRIu64 " KB", bytes / kilobyte);

	} else if (bytes < gigabyte) {
		sprintf(num, "%" PRIu64 " MB", bytes / megabyte);

	} else if (bytes < terabyte) {
		sprintf(num, "%" PRIu64 " GB", bytes / gigabyte);

	} else {
		sprintf(num, "%" PRIu64 " B", bytes);
	}
	return (num);
}

/*
 * Read/Write helpers to ensure a full chunk is read or written
 * unless there is an error.
 * Additionally can be given an offset in the buf where the data
 * should be inserted.
 */
int64_t
Read(int fd, void *buf, uint64_t count)
{
	int64_t rcount, rem;
	uchar_t *cbuf;

	rem = count;
	cbuf = (uchar_t *)buf;
	do {
		rcount = read(fd, cbuf, rem);
		if (rcount < 0) return (rcount);
		if (rcount == 0) break;
		rem = rem - rcount;
		cbuf += rcount;
	} while (rem);
	return (count - rem);
}

/*
 * Read the requested chunk and return the last rabin boundary in the chunk.
 * This helps in splitting chunks at rabin boundaries rather than fixed points.
 * The request buffer may have some data at the beginning carried over from
 * after the previous rabin boundary.
 */
int64_t
Read_Adjusted(int fd, uchar_t *buf, uint64_t count, int64_t *rabin_count, void *ctx, void *pctx)
{
        uchar_t *buf2;
        int64_t rcount;
        dedupe_context_t *rctx = (dedupe_context_t *)ctx;

        if (!ctx) {
		if (pctx)
			return (archiver_read(pctx, buf, count));
		else
			return (Read(fd, buf, count));
	}
        buf2 = buf;
        if (*rabin_count) {
                buf2 = buf + *rabin_count;
                count -= *rabin_count;
        }
	if (pctx)
		rcount = archiver_read(pctx, buf2, count);
	else
		rcount = Read(fd, buf2, count);
        if (rcount > 0) {
                rcount += *rabin_count;
		if (rcount == count) {
			uint64_t rc, rbc;
			rc = rcount;
			rbc = *rabin_count;

			/*
			 * This call does not actually dedupe but finds the last rabin boundary
			 * in the buf.
			 */
			dedupe_compress(rctx, buf, &rc, 0, &rbc, 0);
			rcount = rc;
			*rabin_count = rbc;
		} else {
			*rabin_count = 0;
		}
        } else {
                if (rcount == 0) rcount = *rabin_count;
                *rabin_count = 0;
        }
        return (rcount);
}

int64_t
Write(int fd, const void *buf, uint64_t count)
{
	int64_t wcount, rem;
	uchar_t *cbuf;

	rem = count;
	cbuf = (uchar_t *)buf;
	do {
		wcount = write(fd, cbuf, rem);
		if (wcount < 0) return (wcount);
		rem = rem - wcount;
		cbuf += wcount;
	} while (rem);
	return (count - rem);
}

void
init_algo_props(algo_props_t *props)
{
	props->buf_extra = 0;
	props->compress_mt_capable = 0;
	props->decompress_mt_capable = 0;
	props->single_chunk_mt_capable = 0;
	props->is_single_chunk = 0;
	props->nthreads = 1;
	props->c_max_threads = 1;
	props->d_max_threads = 1;
	props->delta2_span = 0;
}

/*
 * Thread sizing. We want a balanced combination of chunk threads and compression
 * algorithm threads that best fit the available/allowed number of processors.
 */
void
set_threadcounts(algo_props_t *props, int *nthreads, int nprocs, algo_threads_type_t typ) {
	int mt_capable;

	if (typ == COMPRESS_THREADS)
		mt_capable = props->compress_mt_capable;
	else
		mt_capable = props->decompress_mt_capable;

	if (mt_capable) {
		int nthreads1, p_max;

		if (nprocs == 3) {
			props->nthreads = 1;
			*nthreads = 3;
			return;
		}

		if (typ == COMPRESS_THREADS)
			p_max = props->c_max_threads;
		else
			p_max = props->d_max_threads;

		nthreads1 = 1;
		props->nthreads = 1;
		while (nthreads1 < *nthreads || props->nthreads < p_max) {
			if ((props->nthreads+1) * nthreads1 <= nprocs && props->nthreads < p_max) {
				props->nthreads++;

			} else if (props->nthreads * (nthreads1+1) <= nprocs && nthreads1 < *nthreads) {
				++nthreads1;
			} else {
				break;
			}
		}
		*nthreads = nthreads1;

	} else if (props->single_chunk_mt_capable && props->is_single_chunk) {
		*nthreads = 1;
		if (typ == COMPRESS_THREADS)
			props->nthreads = props->c_max_threads;
		else
			props->nthreads = props->d_max_threads;
		if (props->nthreads > nprocs)
			props->nthreads = nprocs;
	}
}

uint64_t
get_total_ram()
{
#ifndef __APPLE__
	uint64_t phys_pages, page_size;

	page_size = sysconf(_SC_PAGESIZE);
	phys_pages = sysconf(_SC_PHYS_PAGES);
	return (phys_pages * page_size);
#else
	int mib[2];
	int64_t size;
	size_t len;
	mib[0] = CTL_HW;
	mib[1] = HW_MEMSIZE;
	size = 0;
	len = sizeof (size);
	if (sysctl(mib, 2, &size, &len, NULL, 0) == 0)
		return (uint64_t)size;
	return (ONE_GB);
#endif
}

#ifdef __APPLE__
int
clock_gettime(int clk_id, struct timespec *ts)
{
	if (clk_id == CLOCK_MONOTONIC) {
		uint64_t abstime = mach_absolute_time();
		return (abstime * sTimebaseInfo.numer / sTimebaseInfo.denom);
	}
	return (0);
}
#endif

double
get_wtime_millis(void)
{
	struct timespec ts;
	int rv;

	rv = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (rv == 0)
		return (ts.tv_sec * 1000 + ((double)ts.tv_nsec) / 1000000L);
	return (1);
}

double
get_mb_s(uint64_t bytes, double strt, double en)
{
	double bytes_sec;

	bytes_sec = ((double)bytes / (en - strt)) * 1000;
	return (BYTES_TO_MB(bytes_sec));
}

void
get_sys_limits(my_sysinfo *msys_info)
{
	unsigned long totram;
	int rv;
	char *val;

#ifdef __APPLE__
	mach_port_t             host_port = mach_host_self();
	unsigned int		host_size = HOST_VM_INFO64_COUNT;
	vm_size_t               pagesize;
	vm_statistics64_data_t    vm_stat;

	host_page_size(host_port, &pagesize);
	rv = host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_size);
	if (rv != KERN_SUCCESS) {
		vm_stat.free_count = (100 * 1024 * 1024) / pagesize; // 100M arbitrary
	}
	uint64_t mem_used = (vm_stat.active_count + vm_stat.inactive_count + vm_stat.wire_count) * pagesize;
	msys_info->freeram = vm_stat.free_count * pagesize;
	msys_info->totalram = mem_used + msys_info->freeram;
	msys_info->totalswap = 0;
	msys_info->freeswap = 0;
	msys_info->mem_unit = pagesize;
	msys_info->sharedram = vm_stat.wire_count * pagesize;

#else
	struct sysinfo sys_info;
	rv = sysinfo(&sys_info);

	if (rv == -1) {
		memset(&sys_info, 0, sizeof (struct sysinfo));
		sys_info.freeram = 100 * 1024 * 1024; // 100M arbitrary
		sys_info.mem_unit = 1;
	}
	msys_info->totalram = sys_info.totalram * sys_info.mem_unit;
	msys_info->freeram = sys_info.freeram * sys_info.mem_unit + sys_info.bufferram * sys_info.mem_unit;
	msys_info->totalswap = sys_info.totalswap * sys_info.mem_unit;
	msys_info->freeswap = sys_info.freeswap * sys_info.mem_unit;
	msys_info->mem_unit = sys_info.mem_unit;
	msys_info->sharedram = sys_info.sharedram * sys_info.mem_unit;
#endif

	/*
	 * If free memory is less than half of total memory (excluding shared allocations),
	 * and at least 75% of swap is free then adjust free memory value to 75% of
	 * total memory excluding shared allocations.
	 */
	totram = msys_info->totalram - msys_info->sharedram;
	if (msys_info->freeram <= (totram >> 1) &&
	    msys_info->freeswap >= ((msys_info->totalswap >> 1) + (msys_info->totalswap >> 2))) {
		msys_info->freeram = (totram >> 1) + (totram >> 2);
	}

	if ((val = getenv("PCOMPRESS_INDEX_MEM")) != NULL) {
		uint64_t mem;

		/*
		 * Externally specified index limit in MB.
		 */
		mem = strtoull(val, NULL, 0);
		mem *= (1024 * 1024);
		if (mem >= (1024 * 1024) && mem < msys_info->freeram) {
			msys_info->freeram = mem;
		}
	} else {
		/*
		 * Use a maximum of approx 75% of free RAM for the index(if limit was not specified).
		 */
		msys_info->freeram = (msys_info->freeram >> 1) + (msys_info->freeram >> 2);
	}
}

int
chk_dir(char *dir)
{
	struct stat st;

	if (stat(dir, &st) == -1) {
		return (0);
	}
	if (!S_ISDIR(st.st_mode)) {
		return (0);
	}
	return (1);
}

/*
 * Simple logging functions. Used for all error and info messages.
 * Default log destination is STDOUT.
 */
void DLL_EXPORT
set_log_dest(log_dest_t *dest)
{
	ldest.type = dest->type;
	ldest.fd = dest->fd;
	ldest.cb = dest->cb;
}

void DLL_EXPORT
set_log_level(int level)
{
	if (level >= 0 && level < 10)
		cur_log_level = level;
}

void DLL_EXPORT
log_msg(log_level_t log_level, int show_errno, const char *format, ...)
{
	int err = errno, written;
	va_list args;
	char msg[1024];

	if (log_level > cur_log_level) return;

	va_start(args, format);
	written = vsnprintf(msg, 1024, format, args);
	va_end(args);

	written += snprintf(msg + written, 1024 - written, "\n");
	if (written < 1024 && show_errno) {
		snprintf(msg + written, 1024 - written, "\nError: %s\n", strerror(err));
	}

	if (ldest.type == LOG_OUTPUT) {
		fputs(msg, stderr);

	} else if (ldest.type == LOG_FILE) {
		int rv;
		rv = write(ldest.fd, msg, strlen(msg));
	} else {
		ldest.cb(msg);
	}
}

char *
get_temp_dir()
{
	char *tmp;
	char tmpdir[MAXPATHLEN];

	tmp = getenv("PCOMPRESS_CACHE_DIR");
	if (tmp == NULL || !chk_dir(tmp)) {
		tmp = getenv("TMPDIR");
		if (tmp == NULL || !chk_dir(tmp)) {
			tmp = getenv("HOME");
			if (tmp == NULL || !chk_dir(tmp)) {
				if (getcwd(tmpdir, MAXPATHLEN) == NULL) {
					tmp = "/tmp";
				} else {
					tmp = tmpdir;
				}
			}
		}
	}
	return (strdup(tmp));
}

/*
 * Temporary file cleanup routines for SIGINT. Maintain a list of
 * filenames to be removed in the signal handler.
 */
void
Int_Handler(int signo)
{
	int i;

	for (i = 0; i < f_count; i++) {
		if (f_name_list[i] != NULL) {
			unlink(f_name_list[i]);
			f_name_list[i] = NULL;
		}
	}
	exit(1);
}

void
handle_signals()
{
	pthread_mutex_lock(&f_mutex);
	if (!f_inited) {
		memset(f_name_list, 0, sizeof (f_name_list));
	}
	pthread_mutex_unlock(&f_mutex);
	signal(SIGINT, Int_Handler);
	signal(SIGTERM, Int_Handler);
}

void
add_fname(char *fn)
{
	int i;

	pthread_mutex_lock(&f_mutex);
	for (i = 0; i < f_count; i++) {
		if (f_name_list[i] == NULL) {
			f_name_list[i] = fn;
			break;
		}
	}
	pthread_mutex_unlock(&f_mutex);
}

void
rm_fname(char *fn)
{
	int i;

	pthread_mutex_lock(&f_mutex);
	for (i = 0; i < f_count; i++) {
		if (f_name_list[i] != NULL) {
			f_name_list[i] = fn;
			break;
		}
	}
	pthread_mutex_unlock(&f_mutex);
}

int
is_incompressible(int type)
{
	int ic = 0;
	int st = PC_SUBTYPE(type);

	ic = (st == TYPE_JPEG) | (st == TYPE_PACKJPG) | (st == TYPE_AUDIO_COMPRESSED);
	return (ic);
}
