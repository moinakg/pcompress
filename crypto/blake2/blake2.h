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
   BLAKE2 reference source code package - optimized C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along with
   this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
*/
#pragma once
#ifndef __BLAKE2_H__
#define __BLAKE2_H__

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#define BLAKE_ALIGN(x) __declspec(align(x))
#else
#define BLAKE_ALIGN(x) __attribute__ ((__aligned__(x)))
#endif

#if defined(__cplusplus)
extern "C" {
#endif

  enum blake2b_constant
  {
    BLAKE2B_BLOCKBYTES = 128,
    BLAKE2B_OUTBYTES   = 64,
    BLAKE2B_KEYBYTES   = 64,
    BLAKE2B_SALTBYTES  = 16,
    BLAKE2B_PERSONALBYTES = 16
  };

#pragma pack(push, 1)
  typedef struct __blake2b_param
  {
    uint8_t  digest_length; // 1
    uint8_t  key_length;    // 2
    uint8_t  fanout;        // 3
    uint8_t  depth;         // 4
    uint32_t leaf_length;   // 8
    uint64_t node_offset;   // 16
    uint8_t  node_depth;    // 17
    uint8_t  inner_length;  // 18
    uint8_t  reserved[14];  // 32
    uint8_t  salt[BLAKE2B_SALTBYTES]; // 48
    uint8_t  personal[BLAKE2B_PERSONALBYTES];  // 64
  } blake2b_param;

  BLAKE_ALIGN( 64 ) typedef struct __blake2b_state
  {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t  buf[2 * BLAKE2B_BLOCKBYTES];
    size_t   buflen;
    uint8_t  last_node;
  } blake2b_state;

  BLAKE_ALIGN( 64 ) typedef struct __blake2bp_state
  {
    blake2b_state S[4][1];
    blake2b_state R[1];
    uint8_t buf[4 * BLAKE2B_BLOCKBYTES];
    size_t  buflen;
  } blake2bp_state;
#pragma pack(pop)

  // Streaming API
  int blake2b_init_sse2( blake2b_state *S, const uint8_t outlen );
  int blake2b_init_key_sse2( blake2b_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2b_init_param_sse2( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_sse2( blake2b_state *S, const uint8_t *in, uint64_t inlen );
  int blake2b_final_sse2( blake2b_state *S, uint8_t *out, uint8_t outlen );
  int blake2bp_init_sse2( blake2bp_state *S, const uint8_t outlen );
  int blake2bp_init_key_sse2( blake2bp_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2bp_update_sse2( blake2bp_state *S, const uint8_t *in, uint64_t inlen );
  int blake2bp_final_sse2( blake2bp_state *S, uint8_t *out, uint8_t outlen );

  int blake2b_init_ssse3( blake2b_state *S, const uint8_t outlen );
  int blake2b_init_key_ssse3( blake2b_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2b_init_param_ssse3( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_ssse3( blake2b_state *S, const uint8_t *in, uint64_t inlen );
  int blake2b_final_ssse3( blake2b_state *S, uint8_t *out, uint8_t outlen );
  int blake2bp_init_ssse3( blake2bp_state *S, const uint8_t outlen );
  int blake2bp_init_key_ssse3( blake2bp_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2bp_update_ssse3( blake2bp_state *S, const uint8_t *in, uint64_t inlen );
  int blake2bp_final_ssse3( blake2bp_state *S, uint8_t *out, uint8_t outlen );

  int blake2b_init_sse41( blake2b_state *S, const uint8_t outlen );
  int blake2b_init_key_sse41( blake2b_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2b_init_param_sse41( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_sse41( blake2b_state *S, const uint8_t *in, uint64_t inlen );
  int blake2b_final_sse41( blake2b_state *S, uint8_t *out, uint8_t outlen );
  int blake2bp_init_sse41( blake2bp_state *S, const uint8_t outlen );
  int blake2bp_init_key_sse41( blake2bp_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2bp_update_sse41( blake2bp_state *S, const uint8_t *in, uint64_t inlen );
  int blake2bp_final_sse41( blake2bp_state *S, uint8_t *out, uint8_t outlen );

  int blake2b_init_avx( blake2b_state *S, const uint8_t outlen );
  int blake2b_init_key_avx( blake2b_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2b_init_param_avx( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_avx( blake2b_state *S, const uint8_t *in, uint64_t inlen );
  int blake2b_final_avx( blake2b_state *S, uint8_t *out, uint8_t outlen );
  int blake2bp_init_avx( blake2bp_state *S, const uint8_t outlen );
  int blake2bp_init_key_avx( blake2bp_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  int blake2bp_update_avx( blake2bp_state *S, const uint8_t *in, uint64_t inlen );
  int blake2bp_final_avx( blake2bp_state *S, uint8_t *out, uint8_t outlen );

  // Simple API
  int blake2b_sse2( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );
  int blake2bp_sse2( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );

  int blake2b_ssse3( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );
  int blake2bp_ssse3( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );

  int blake2b_sse41( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );
  int blake2bp_sse41( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );

  int blake2b_avx( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );
  int blake2bp_avx( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );

#if defined(__cplusplus)
}
#endif

#endif

