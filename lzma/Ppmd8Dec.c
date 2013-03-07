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

/* Ppmd8Dec.c -- PPMdI Decoder
2010-04-16 : Igor Pavlov : Public domain
This code is based on:
  PPMd var.I (2002): Dmitry Shkarin : Public domain
  Carryless rangecoder (1999): Dmitry Subbotin : Public domain */

#include "Ppmd8.h"

#define kTop (1 << 24)
#define kBot (1 << 15)

Bool Ppmd8_RangeDec_Init(CPpmd8 *p)
{
  unsigned i;
  p->Low = 0;
  p->Range = 0xFFFFFFFF;
  p->Code = 0;
  for (i = 0; i < 4; i++) {
    p->Code = (p->Code << 8) | p->buf[p->bufUsed];
    p->bufUsed++;
  }
  return (p->Code < 0xFFFFFFFF);
}

static UInt32 RangeDec_GetThreshold(CPpmd8 *p, UInt32 total)
{
  return p->Code / (p->Range /= total);
}

static void RangeDec_Decode(CPpmd8 *p, UInt32 start, UInt32 size)
{
  start *= p->Range;
  p->Low += start;
  p->Code -= start;
  p->Range *= size;

  while ((p->Low ^ (p->Low + p->Range)) < kTop ||
      (p->Range < kBot && ((p->Range = (0 - p->Low) & (kBot - 1)), 1)))
  {
    p->Code = (p->Code << 8) | p->buf[p->bufUsed];
    p->bufUsed++;
    p->Range <<= 8;
    p->Low <<= 8;
  }
}

#define MASK(sym) ((signed char *)charMask)[sym]
int Ppmd8_DecodeSymbol(CPpmd8 *p)
{
  int res;
  size_t dl;
  Byte buf[1];

  res = Ppmd8_DecodeToBuffer(p, buf, 1, &dl);
  if (res < 0 || dl == 0)
    return (res);
  return (buf[0]);
}

int Ppmd8_DecodeToBuffer(CPpmd8 *p, Byte *buf, size_t buflen, size_t *decodedlen)
{
  size_t charMask[256 / sizeof(size_t)];
  size_t _cnt;
  int _sym;

  *decodedlen = 0;
  for (_cnt = 0; _cnt < buflen; _cnt++) {
  if (p->MinContext->NumStats != 0)
  {
    CPpmd_State *s = Ppmd8_GetStats(p, p->MinContext);
    unsigned i;
    UInt32 count, hiCnt;
    if ((count = RangeDec_GetThreshold(p, p->MinContext->SummFreq)) < (hiCnt = s->Freq))
    {
      Byte symbol;
      RangeDec_Decode(p, 0, s->Freq);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd8_Update1_0(p);
      _sym = symbol;
      goto next_iter;
      //return symbol;
    }
    p->PrevSuccess = 0;
    i = p->MinContext->NumStats;
    do
    {
      if ((hiCnt += (++s)->Freq) > count)
      {
        Byte symbol;
        RangeDec_Decode(p, hiCnt - s->Freq, s->Freq);
        p->FoundState = s;
        symbol = s->Symbol;
        Ppmd8_Update1(p);
        _sym = symbol;
	goto next_iter;
        //return symbol;
      }
    }
    while (--i);
    if (count >= p->MinContext->SummFreq) {
      _sym = -2;
      goto next_iter;
      //return -2;
    }
    RangeDec_Decode(p, hiCnt, p->MinContext->SummFreq - hiCnt);
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats;
    do { MASK((--s)->Symbol) = 0; } while (--i);
  }
  else
  {
    UInt16 *prob = Ppmd8_GetBinSumm(p);
    if (((p->Code / (p->Range >>= 14)) < *prob))
    {
      Byte symbol;
      RangeDec_Decode(p, 0, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      symbol = (p->FoundState = Ppmd8Context_OneState(p->MinContext))->Symbol;
      Ppmd8_UpdateBin(p);
      _sym = symbol;
      goto next_iter;
      //return symbol;
    }
    RangeDec_Decode(p, *prob, (1 << 14) - *prob);
    *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
    p->InitEsc = PPMD8_kExpEscape[*prob >> 10];
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(Ppmd8Context_OneState(p->MinContext)->Symbol) = 0;
    p->PrevSuccess = 0;
  }
  for (;;)
  {
    CPpmd_State *ps[256], *s;
    UInt32 freqSum, count, hiCnt;
    CPpmd_See *see;
    unsigned i, num, numMasked = p->MinContext->NumStats;
    do
    {
      p->OrderFall++;
      if (!p->MinContext->Suffix) {
        _sym = -1;
        goto next_iter;
        //return -1;
      }
      p->MinContext = Ppmd8_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    hiCnt = 0;
    s = Ppmd8_GetStats(p, p->MinContext);
    i = 0;
    num = p->MinContext->NumStats - numMasked;
    do
    {
      int k = (int)(MASK(s->Symbol));
      hiCnt += (s->Freq & k);
      ps[i] = s++;
      i -= k;
    }
    while (i != num);
    
    see = Ppmd8_MakeEscFreq(p, numMasked, &freqSum);
    freqSum += hiCnt;
    count = RangeDec_GetThreshold(p, freqSum);
    
    if (count < hiCnt)
    {
      Byte symbol;
      CPpmd_State **pps = ps;
      for (hiCnt = 0; (hiCnt += (*pps)->Freq) <= count; pps++);
      s = *pps;
      RangeDec_Decode(p, hiCnt - s->Freq, s->Freq);
      Ppmd_See_Update(see);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd8_Update2(p);
      _sym = symbol;
      goto next_iter;
      //return symbol;
    }
    if (count >= freqSum) {
      _sym = -2;
      goto next_iter;
      //return -2;
    }
    RangeDec_Decode(p, hiCnt, freqSum - hiCnt);
    see->Summ = (UInt16)(see->Summ + freqSum);
    do { MASK(ps[--i]->Symbol) = 0; } while (i != 0);
  }
next_iter:
  if (_sym < -1) return (_sym);
  (*decodedlen)++;
  if (_sym < 0) return (_sym);
  buf[_cnt] = _sym;
  }
  return (0);
}
