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
 *      
 * This program includes partly-modified public domain source
 * code from the LZMA SDK: http://www.7-zip.org/sdk.html
 */

#include <sys/types.h>
#include <sys/param.h>
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
#include <link.h>
#include <rabin_dedup.h>
#include <skein.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <sha256.h>
#include <crypto_aes.h>

#include "utils.h"
#include "cpuid.h"

#define	PROVIDER_OPENSSL	0
#define	PROVIDER_X64_OPT	1

static void init_sha256(void);
static int geturandom_bytes(uchar_t rbytes[32]);
/*
 * Checksum properties
 */
typedef void (*ckinit_func_ptr)(void);
static struct {
	char	*name;
	cksum_t	cksum_id;
	int	bytes;
	ckinit_func_ptr init_func;
} cksum_props[] = {
	{"CRC64",	CKSUM_CRC64,	8,	NULL},
	{"SKEIN256",	CKSUM_SKEIN256,	32,	NULL},
	{"SKEIN512",	CKSUM_SKEIN512,	64,	NULL},
	{"SHA256",	CKSUM_SHA256,	32,	init_sha256},
	{"SHA512",	CKSUM_SHA512,	64,	NULL}
};


static int cksum_provider = PROVIDER_OPENSSL, ossl_inited = 0;

extern uint64_t lzma_crc64(const uint8_t *buf, size_t size, uint64_t crc);
extern uint64_t lzma_crc64_8bchk(const uint8_t *buf, size_t size,
	uint64_t crc, uint64_t *cnt);

void
err_exit(int show_errno, const char *format, ...)
{
	int err = errno;
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	if (show_errno)
		fprintf(stderr, "\nError: %s\n", strerror(err));
	exit(1);
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
 * The number should fit in an ssize_t data type.
 * Numeric overflow is also checked. The routine parse_numeric() returns
 * 1 if there was a numeric overflow.
 */
static int
raise_by_multiplier(ssize_t *val, int mult, int power) {
	ssize_t result;

	while (power-- > 0) {
		result = *val * mult;
		if (result/mult != *val)
			return (1);
		*val = result;
	}
	return (0);
}

int
parse_numeric(ssize_t *val, const char *str)
{
	int i, ovr;
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
		sprintf(num, "%llu B", bytes);

	} else if (bytes < megabyte) {
		sprintf(num, "%llu KB", bytes / kilobyte);

	} else if (bytes < gigabyte) {
		sprintf(num, "%llu MB", bytes / megabyte);

	} else if (bytes < terabyte) {
		sprintf(num, "%llu GB", bytes / gigabyte);

	} else {
		sprintf(num, "%llu B", bytes);
	}
	return (num);
}

/*
 * Read/Write helpers to ensure a full chunk is read or written
 * unless there is an error.
 * Additionally can be given an offset in the buf where the data
 * should be inserted.
 */
ssize_t
Read(int fd, void *buf, size_t count)
{
	ssize_t rcount, rem;
	uchar_t *cbuf;
	va_list args;

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
ssize_t
Read_Adjusted(int fd, uchar_t *buf, size_t count, ssize_t *rabin_count, void *ctx)
{
        char *buf2;
        ssize_t rcount;
        dedupe_context_t *rctx = (dedupe_context_t *)ctx;

        if (!ctx)  return (Read(fd, buf, count));
        buf2 = buf;
        if (*rabin_count) {
                buf2 = (char *)buf + *rabin_count;
                count -= *rabin_count;
        }
        rcount = Read(fd, buf2, count);
        if (rcount > 0) {
                rcount += *rabin_count;
		if (rcount == count)
			dedupe_compress(rctx, buf, &rcount, 0, rabin_count);
		else
			*rabin_count = 0;
        } else {
                if (rcount == 0) rcount = *rabin_count;
                *rabin_count = 0;
        }
        return (rcount);
}

ssize_t
Write(int fd, const void *buf, size_t count)
{
	ssize_t wcount, rem;
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
				nthreads1++;
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

int
compute_checksum(uchar_t *cksum_buf, int cksum, uchar_t *buf, ssize_t bytes)
{
	if (cksum == CKSUM_CRC64) {
		uint64_t *ck = (uint64_t *)cksum_buf;
		*ck = lzma_crc64(buf, bytes, 0);

	} else if (cksum == CKSUM_SKEIN256) {
		Skein_512_Ctxt_t ctx;

		Skein_512_Init(&ctx, 256);
		Skein_512_Update(&ctx, buf, bytes);
		Skein_512_Final(&ctx, cksum_buf);

	} else if (cksum == CKSUM_SKEIN512) {
		Skein_512_Ctxt_t ctx;

		Skein_512_Init(&ctx, 512);
		Skein_512_Update(&ctx, buf, bytes);
		Skein_512_Final(&ctx, cksum_buf);

	} else if (cksum == CKSUM_SHA256) {
		if (cksum_provider == PROVIDER_OPENSSL) {
			SHA256_CTX ctx;

			SHA256_Init(&ctx);
			SHA256_Update(&ctx, buf, bytes);
			SHA256_Final(cksum_buf, &ctx);
		} else {
			SHA256_Context ctx;

			opt_SHA256_Init(&ctx);
			opt_SHA256_Update(&ctx, buf, bytes);
			opt_SHA256_Final(&ctx, cksum_buf);
		}
	} else if (cksum == CKSUM_SHA512) {
		SHA512_CTX ctx;

		SHA512_Init(&ctx);
		SHA512_Update(&ctx, buf, bytes);
		SHA512_Final(cksum_buf, &ctx);
	} else {
		return (-1);
	}
	return (0);
}

static void
init_sha256(void)
{
#ifdef	WORDS_BIGENDIAN
	cksum_provider = PROVIDER_OPENSSL;
#else
#ifdef	__x86_64__
	processor_info_t pc;

	cksum_provider = PROVIDER_OPENSSL;
	cpuid_basic_identify(&pc);
	if (pc.proc_type == PROC_X64_INTEL || pc.proc_type == PROC_X64_AMD) {
		if (opt_Init_SHA(&pc) == 0) {
			cksum_provider = PROVIDER_X64_OPT;
		}
	}
#endif
#endif
}

/*
 * Check is either the given checksum name or id is valid and
 * return it's properties.
 */
int
get_checksum_props(char *name, int *cksum, int *cksum_bytes)
{
	int i;

	for (i=0; i<sizeof (cksum_props); i++) {
		if ((name != NULL && strcmp(name, cksum_props[i].name) == 0) ||
		    (*cksum != 0 && *cksum == cksum_props[i].cksum_id)) {
			*cksum = cksum_props[i].cksum_id;
			*cksum_bytes = cksum_props[i].bytes;
			if (cksum_props[i].init_func)
				cksum_props[i].init_func();
			return (0);
		}
	}
	return (-1);
}

/*
 * Endian independent way of storing the checksum bytes. This is actually
 * storing in little endian format and a copy can be avoided in x86 land.
 * However unsightly ifdefs are avoided here since this is not so performance
 * critical.
 */
void
serialize_checksum(uchar_t *checksum, uchar_t *buf, int cksum_bytes)
{
	int i,j;

	j = 0;
	for (i=cksum_bytes; i>0; i--) {
		buf[j] = checksum[i-1];
		j++;
	}
}

void
deserialize_checksum(uchar_t *checksum, uchar_t *buf, int cksum_bytes)
{
	int i,j;

	j = 0;
	for (i=cksum_bytes; i>0; i--) {
		checksum[i-1] = buf[j];
		j++;
	}
}

int
init_crypto(crypto_ctx_t *cctx, uchar_t *pwd, int pwd_len, int crypto_alg,
	    uchar_t *salt, int saltlen, uint64_t nonce, int enc_dec)
{
	if (crypto_alg == CRYPTO_ALG_AES) {
		aes_ctx_t *actx = malloc(sizeof (aes_ctx_t));

		if (enc_dec) {
			/*
			 * Encryption init.
			 */
			cctx->salt = malloc(32);
			salt = cctx->salt;
			cctx->saltlen = 32;
			if (RAND_status() != 1 || RAND_bytes(salt, 32) != 1) {
				if (geturandom_bytes(salt) != 0) {
					uchar_t sb[64];
					int b;
					struct timespec tp;

					b = 0;
					/* No good random pool is populated/available. What to do ? */
					if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1) {
						time((time_t *)&sb[b]);
						b += 8;
					} else {
						uint64_t v;
						v = tp.tv_sec * 1000UL + tp.tv_nsec;
						*((uint64_t *)&sb[b]) = v;
						b += 8;
					}
					*((uint32_t *)&sb[b]) = rand();
					b += 4;
					*((uint32_t *)&sb[b]) = getpid();
					b += 4;
					compute_checksum(&sb[b], CKSUM_SHA256, sb, b);
					b = 8 + 4;
					*((uint32_t *)&sb[b]) = rand();
					compute_checksum(salt, CKSUM_SHA256, &sb[b], 32 + 4);
				}
			}

			/*
			 * Zero nonce (arg #6) since it will be generated.
			 */
			if (aes_init(actx, salt, 32, pwd, pwd_len, 0, enc_dec) != 0) {
				fprintf(stderr, "Failed to initialize AES context\n");
				return (-1);
			}
		} else {
			/*
			 * Decryption init.
			 * Pass given nonce and salt.
			 */
			if (saltlen > MAX_SALTLEN) {
				fprintf(stderr, "Salt too long. Max allowed length is %d\n",
				    MAX_SALTLEN);
				return (-1);
			}
			cctx->salt = malloc(saltlen);
			memcpy(cctx->salt, salt, saltlen);

			if (aes_init(actx, cctx->salt, saltlen, pwd, pwd_len, nonce,
			    enc_dec) != 0) {
				fprintf(stderr, "Failed to initialize AES context\n");
				return (-1);
			}
		}
		cctx->crypto_ctx = actx;
		cctx->crypto_alg = crypto_alg;
		cctx->enc_dec = enc_dec;
	} else {
		fprintf(stderr, "Unrecognized algorithm code: %d\n", crypto_alg);
		return (-1);
	}
	return (0);
}

int
crypto_buf(crypto_ctx_t *cctx, uchar_t *from, uchar_t *to, ssize_t bytes, uint64_t id)
{
	if (cctx->crypto_alg == CRYPTO_ALG_AES) {
		if (cctx->enc_dec == ENCRYPT_FLAG) {
			return (aes_encrypt(cctx->crypto_ctx, from, to, bytes, id));
		} else {
			return (aes_decrypt(cctx->crypto_ctx, from, to, bytes, id));
		}
	} else {
		fprintf(stderr, "Unrecognized algorithm code: %d\n", cctx->crypto_alg);
		return (-1);
	}
	return (0);
}

uint64_t
crypto_nonce(crypto_ctx_t *cctx)
{
	return (aes_nonce(cctx->crypto_ctx));
}

void
cleanup_crypto(crypto_ctx_t *cctx)
{
	aes_cleanup(cctx->crypto_ctx);
	memset(cctx->salt, 0, 32);
	free(cctx->salt);
	free(cctx);
}

static int
geturandom_bytes(uchar_t rbytes[32])
{
	int fd;
	ssize_t lenread;
	uchar_t * buf = rbytes;
	size_t buflen = 32;

	/* Open /dev/urandom. */
	if ((fd = open("/dev/urandom", O_RDONLY)) == -1)
		goto err0;
	
	/* Read bytes until we have filled the buffer. */
	while (buflen > 0) {
		if ((lenread = read(fd, buf, buflen)) == -1)
			goto err1;
		
		/* The random device should never EOF. */
		if (lenread == 0)
			goto err1;
		
		/* We're partly done. */
		buf += lenread;
		buflen -= lenread;
	}
	
	/* Close the device. */
	while (close(fd) == -1) {
		if (errno != EINTR)
			goto err0;
	}
	
	/* Success! */
	return (0);
err1:
	close(fd);
err0:
	/* Failure! */
	return (4);
}

int
get_pw_string(char pw[MAX_PW_LEN], char *prompt)
{
	int fd, len;
	FILE *input, *strm;
	struct termios oldt, newt;
	uchar_t pw1[MAX_PW_LEN], pw2[MAX_PW_LEN], *s;

	// Try TTY first
	fd = open("/dev/tty", O_RDWR | O_NOCTTY);
	if (fd != -1) {
		input = fdopen(fd, "w+");
		strm = input;
	} else {
		// Fall back to stdin
		fd = STDIN_FILENO;
		input = stdin;
		strm = stderr;
	}
	tcgetattr(fd, &oldt);
	newt = oldt;
	newt.c_lflag &= ~ECHO;
	tcsetattr(fd, TCSANOW, &newt);

	fprintf(stderr, "%s: ", prompt);
	fflush(stderr);
	s = fgets(pw1, MAX_PW_LEN, input);
	fputs("\n", stderr);

	if (s == NULL) {
		tcsetattr(fd, TCSANOW, &oldt);
		fflush(strm);
		return (-1);
	}

	fprintf(stderr, "%s (once more): ", prompt);
	fflush(stderr);
	s = fgets(pw2, MAX_PW_LEN, input);
	tcsetattr(fd, TCSANOW, &oldt);
	fflush(strm);
	fputs("\n", stderr);

	if (s == NULL) {
		return (-1);
	}

	if (strcmp(pw1, pw2) != 0) {
		fprintf(stderr, "Passwords do not match!\n");
		memset(pw1, 0, MAX_PW_LEN);
		memset(pw2, 0, MAX_PW_LEN);
		return (-1);
	}

	len = strlen(pw1);
	pw1[len-1] = '\0';
	strcpy(pw, pw1);
	memset(pw1, 0, MAX_PW_LEN);
	memset(pw2, 0, MAX_PW_LEN);
	return (len);
}
