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

/*-
 * Copyright (c) 2010, 2011 Allan Saddi <allan@saddi.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

void
HMAC_INIT(HMAC_CONTEXT *ctxt, const void *key, size_t keyLen)
{
  HASH_CONTEXT keyCtxt;
  unsigned int i;
  uint8_t pkey[HASH_BLOCK_SIZE], okey[HASH_BLOCK_SIZE], ikey[HASH_BLOCK_SIZE];

  /* Ensure key is zero-padded */
  memset(pkey, 0, sizeof(pkey));

  if (keyLen > sizeof(pkey)) {
    /* Hash key if > HASH_BLOCK_SIZE */
    HASH_INIT(&keyCtxt);
    HASH_UPDATE(&keyCtxt, key, keyLen);
    HASH_FINAL(&keyCtxt, pkey);
  }
  else {
    memcpy(pkey, key, keyLen);
  }

  /* XOR with opad, ipad */
  for (i = 0; i < sizeof(okey); i++) {
    okey[i] = pkey[i] ^ 0x5c;
  }
  for (i = 0; i < sizeof(ikey); i++) {
    ikey[i] = pkey[i] ^ 0x36;
  }

  /* Initialize hash contexts */
  HASH_INIT(&ctxt->outer);
  HASH_UPDATE(&ctxt->outer, okey, sizeof(okey));
  HASH_INIT(&ctxt->inner);
  HASH_UPDATE(&ctxt->inner, ikey, sizeof(ikey));

  /* Burn the stack */
  memset(ikey, 0, sizeof(ikey));
  memset(okey, 0, sizeof(okey));
  memset(pkey, 0, sizeof(pkey));
  memset(&keyCtxt, 0, sizeof(keyCtxt));
}

void
HMAC_UPDATE(HMAC_CONTEXT *ctxt, const void *data, size_t len)
{
  HASH_UPDATE(&ctxt->inner, data, len);
}

void
HMAC_FINAL(HMAC_CONTEXT *ctxt, uint8_t hmac[HASH_SIZE])
{
  uint8_t ihash[HASH_SIZE];

  HASH_FINAL(&ctxt->inner, ihash);
  HASH_UPDATE(&ctxt->outer, ihash, sizeof(ihash));
  HASH_FINAL(&ctxt->outer, hmac);

  memset(ihash, 0, sizeof(ihash));
}
