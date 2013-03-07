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

/* Ppmd8Enc.c -- PPMdI Encoder
2010-04-16 : Igor Pavlov : Public domain
This code is based on:
  PPMd var.I (2002): Dmitry Shkarin : Public domain
  Carryless rangecoder (1999): Dmitry Subbotin : Public domain */

#include "Ppmd8.h"

#define kTop (1 << 24)
#define kBot (1 << 15)

void Ppmd8_RangeEnc_FlushData(CPpmd8 *p)
{
  unsigned i;
  for (i = 0; i < 4; i++, p->Low <<= 8 ) {
     if (p->bufUsed < p->bufLen) {
       p->buf[p->bufUsed] = (Byte)(p->Low >> 24);
       p->bufUsed++;
     } else {
       p->overflow = 1;
     }
  }
//    p->Stream.Out->Write(p->Stream.Out, (Byte)(p->Low >> 24));
}

static void RangeEnc_Normalize(CPpmd8 *p)
{
  while ((p->Low ^ (p->Low + p->Range)) < kTop ||
      (p->Range < kBot && ((p->Range = (0 - p->Low) & (kBot - 1)), 1)))
  {
    if (p->bufUsed < p->bufLen) {
      p->buf[p->bufUsed] = (Byte)(p->Low >> 24);
      p->bufUsed++;
      //p->Stream.Out->Write(p->Stream.Out, (Byte)(p->Low >> 24));
    } else {
      p->overflow = 1;
    }
    p->Range <<= 8;
    p->Low <<= 8;
  }
}

static void RangeEnc_Encode(CPpmd8 *p, UInt32 start, UInt32 size, UInt32 total)
{
  p->Low += start * (p->Range /= total);
  p->Range *= size;
  RangeEnc_Normalize(p);
}

static void RangeEnc_EncodeBit_0(CPpmd8 *p, UInt32 size0)
{
  p->Range >>= 14;
  p->Range *= size0;
  RangeEnc_Normalize(p);
}

static void RangeEnc_EncodeBit_1(CPpmd8 *p, UInt32 size0)
{
  p->Low += size0 * (p->Range >>= 14);
  p->Range *= ((1 << 14) - size0);
  RangeEnc_Normalize(p);
}


#define MASK(sym) ((signed char *)charMask)[sym]

void Ppmd8_EncodeSymbol(CPpmd8 *p, int symbol)
{
  Byte buf[1];

  if (symbol == -1) {
    Ppmd8_EncodeBuffer(p, NULL, 1);
  } else {
    buf[0] = symbol;
    Ppmd8_EncodeBuffer(p, buf, 1);
  }
}

void Ppmd8_EncodeBuffer(CPpmd8 *p, Byte *buf, size_t buflen)
{
  size_t charMask[256 / sizeof(size_t)];
  size_t _cnt;
  int symbol;

  if (!buf) buflen = 1;
  for (_cnt = 0; _cnt < buflen; _cnt++) {
  if (buf)
    symbol = buf[_cnt];
  else
    symbol = -1;
  if (p->MinContext->NumStats != 0)
  {
    CPpmd_State *s = Ppmd8_GetStats(p, p->MinContext);
    UInt32 sum;
    unsigned i;
    if (s->Symbol == symbol)
    {
      RangeEnc_Encode(p, 0, s->Freq, p->MinContext->SummFreq);
      p->FoundState = s;
      Ppmd8_Update1_0(p);
      //return;
      goto next_iter;
    }
    p->PrevSuccess = 0;
    sum = s->Freq;
    i = p->MinContext->NumStats;
    do
    {
      if ((++s)->Symbol == symbol)
      {
        RangeEnc_Encode(p, sum, s->Freq, p->MinContext->SummFreq);
        p->FoundState = s;
        Ppmd8_Update1(p);
        //return;
	goto next_iter;
      }
      sum += s->Freq;
    }
    while (--i);
    
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats;
    do { MASK((--s)->Symbol) = 0; } while (--i);
    RangeEnc_Encode(p, sum, p->MinContext->SummFreq - sum, p->MinContext->SummFreq);
  }
  else
  {
    UInt16 *prob = Ppmd8_GetBinSumm(p);
    CPpmd_State *s = Ppmd8Context_OneState(p->MinContext);
    if (s->Symbol == symbol)
    {
      RangeEnc_EncodeBit_0(p, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      p->FoundState = s;
      Ppmd8_UpdateBin(p);
      //return;
      goto next_iter;
    }
    else
    {
      RangeEnc_EncodeBit_1(p, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
      p->InitEsc = PPMD8_kExpEscape[*prob >> 10];
      PPMD_SetAllBitsIn256Bytes(charMask);
      MASK(s->Symbol) = 0;
      p->PrevSuccess = 0;
    }
  }
  for (;;)
  {
    UInt32 escFreq;
    CPpmd_See *see;
    CPpmd_State *s;
    UInt32 sum;
    unsigned i, numMasked = p->MinContext->NumStats;
    do
    {
      p->OrderFall++;
      if (!p->MinContext->Suffix)
	goto next_iter;
        //return; /* EndMarker (symbol = -1) */
      p->MinContext = Ppmd8_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    
    see = Ppmd8_MakeEscFreq(p, numMasked, &escFreq);
    s = Ppmd8_GetStats(p, p->MinContext);
    sum = 0;
    i = p->MinContext->NumStats + 1;
    do
    {
      int cur = s->Symbol;
      if (cur == symbol)
      {
        UInt32 low = sum;
        CPpmd_State *s1 = s;
        while (i > 2)
        {
          sum += (s->Freq & (int)(MASK(s->Symbol)));
          s++; i--;
          sum += (s->Freq & (int)(MASK(s->Symbol)));
          s++; i--;
        }
        do
        {
          sum += (s->Freq & (int)(MASK(s->Symbol)));
          s++;
        }
        while (--i);
        RangeEnc_Encode(p, low, s1->Freq, sum + escFreq);
        Ppmd_See_Update(see);
        p->FoundState = s1;
        Ppmd8_Update2(p);
        //return;
	goto next_iter;
      }
      sum += (s->Freq & (int)(MASK(cur)));
      MASK(cur) = 0;
      s++;
    }
    while (--i);
    
    RangeEnc_Encode(p, sum, escFreq, sum + escFreq);
    see->Summ = (UInt16)(see->Summ + sum + escFreq);
  }
next_iter:;
  }
}
