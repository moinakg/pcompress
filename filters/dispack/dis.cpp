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

#include "types.hpp"
#include "dis.hpp"
#include <utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __APPLE__
#include <malloc.h>
#endif
#include <assert.h>
#include <iostream>

using namespace std;

/* Version history:
 *
 * 1.00  (Nov 2009)  Initial release
 * 1.01  (Jan 2011)  Don't assert on bytes > MAXINSTR when dealing with jump tables
 * 1.02  (Nov 2013)  (Moinak Ghosh) Changes to integrate with Pcompress.
 *                   Adapted and modified from:
 *                   http://www.farbrausch.de/~fg/code/disfilter/
 */

/****************************************************************************/

/* This is a filter for x86 binary code, intended to improve its compressibility
 * by standard algorithms. The basic ideas are quite old; for example, the LZX
 * algorithm used in Microsoft .CAB files uses a special preprocessor that
 * converts the target address in CALL opcodes from a relative offset to an
 * absolute address. This simple transforms greatly helps both LZ-based and
 * statistical coders: the same function being called repeatedly now results
 * in the same byte sequence for the call being repeated, instead of having
 * a different encoding every time. The preprocessor doesn't really understand
 * the instruction stream; it just looks for a 0xE8 byte (the opcode for near
 * call) and adds the current position to the 4 bytes that follow it.
 *
 * Most modern compressors include this filter or variations, to be used on .EXE
 * files; newer variants usually try to detect whether the target offset would be
 * within the executable image to reduce the number of false positives. Another
 * common modification stores the transformed offsets in big endian byte order:
 * this clusters the high bits (which are likely to be similar along a stretch of
 * code) together with the opcode, again yielding somewhat better compression.
 *
 * However, all this is based on a very limited understanding of x86 binary code.
 * It is possible to do significantly with a more thorough understanding of the
 * bytestream and its underlying structure. This algorithm borrows heavily from the
 * Split-Stream^2 method described in [1] (or, more precisely, an earlier variant
 * published somewhen in 2004; I don't remember the details anymore). It also introduces
 * some (to my knowledge) novel ideas, though.
 *
 * The basic idea behind Split-Stream is to disassemble the target program,
 * splitting it into several distinct streams that can be coded separately. Examples
 * of such streams are the opcodes themselves, 8 bit immediates, 32 bit immediates,
 * jump and call target addresses, and so on - the idea being that the individual
 * fields are highly correlated amongst themselves, but largely independent of each
 * other. Splitting the streams reduces the context dilution (the inclusion of
 * irrelevant values in the context used for prediction) that otherwise harms compression
 * in compiled code. Since the actual compressor in kkrunchy is a LZ-based dictionary
 * coder and not a context coder, there's no easy way to mix multiple models or use
 * alphabets with more than 256 symbols; hence the streams are simply stored sequentially,
 * with a small header denoting the size of each. This interface sacrifices some
 * compression potential, but has the advantage that the filter inputs and outputs
 * simple bytestreams; kkrunchy actually compresses the (several hundred bytes long)
 * unfiltering code along with the transformed code, so part of the decompressor is
 * stored in compressed form. This results in a somewhat peculiar "bootstrapping"
 * decompression process but saved roughly 200 bytes when it was originally written;
 * a big enough gain to be worth it when targeting 64k executables.
 *
 * The actual list of streams that are identified can be found below (the "Streams" enum).
 * To categorize which byte belongs where, the code needs to be disassembled. This
 * is simpler than it sounds, given the complexity of x86 instruction encoding;
 * luckily, there's no need to fully "understand" each instruction. We mainly need to
 * be able to identify the opcode, the addressing mode used, and the presence of
 * immediate data fields. This is implemented using a mostly table-driven disassembler.
 * Since the original decoder was heavily optimized for size and the tables need to be
 * included with the decoder, the encoding is very compact: It mainly consists of two
 * tables of 256 entries each with 4 bits per entry used - the first table describing
 * one-byte opcodes, the second for two-byte opcodes (when this code was written, there
 * were no three-byte opcodes yet). There are some simplifications present in the tables
 * and the disassembler, where doing so poses no problems. For example, all prefixes
 * are treated as one-byte opcodes with no operands; this is incorrect, but as long as
 * the encoder and decoder agree on it, there's no problem. There's also no need to
 * distinguish between different instructions when they all have the same addressing modes
 * and combination of immediate operands. All this gets rid of a lot of special cases.
 * There is one significant deviation from the PPMexe paper [1], though: the code
 * is very careful never to assume that its parsing of the instruction stream is correct,
 * and absolutely no irreversible transforms take place (such as the instruction
 * rescheduling in [1]). Unrecognizable and invalid opcodes are preserved. This is done
 * by using a very uncommon opcode as escape code, encapsulating otherwise invalid
 * sequences within the bytestream. This property is critical in practice: code sections
 * often contain jump tables and other data that isn't decodable as x86 instruction
 * stream. Corrupting such data during the compression process is unacceptable.
 *
 * The target adresses of near jumps and calls of course still get converted from
 * relative to absolute; additionally, all values larger than 8 bit are stored in big
 * endian byte order. Both transforms are trivial to undo on the decoder side and yield
 * notable improvements in compression ratio. Additionally, the last 255 call targets
 * are kept in an array that's updated using the "move to front" heuristic. If a target
 * occurs repeatedly (as is common in practice), the offset doesn't need to be coded at
 * all; instead the position in the array is transmitted. (This is the ST_CALL_IDX
 * stream). Additionally, the instruction stream is analyzed to identify potential
 * call targets (i.e. start addresses of functions) even before they are first
 * referenced: if a RET or INT3 opcode is found in the instruction stream, the filter
 * assumes that the next instruction is likely to start a new function (MSVC++ uses
 * INT3 opcodes to fill the "no man's land" between functions) and adds its address to
 * the function table automatically. Typical overall hit rates for the function table
 * are between 70 and 80 per cent - so only a quarter of all call target addresses ever
 * needs to be stored explicitly.
 *
 * The most common type of data intermixed with code sections is jump tables and
 * virtual function tables. Generally speaking, any data inside the code section is
 * bad for the filter; its statistics are very different from the binary code being
 * encoded which hurts compression, and it causes the disassembler to lose sync
 * temporarily. To work around this problem, the encoder tries to identify jump
 * tables, using another escape code to identify them in the output stream. The
 * heuristic used here is rather simple, but works very well: When an instruction
 * is expected, the encoder looks at the next 12 bytes. If they evaluate to
 * addresses within the code section when interpreted as 3 dwords, the encoder assumes
 * that it has found a jump table (or vtable). Jump table entries are encoded the
 * same way that call targets are.
 *
 * [1] "PPMexe: Program Compression"
 *     M. Drinic, D. Kirovski, and H. Vo, MS Research
 *     ACM Transactions on Programming Languages and Systems, Vol.29, (no.1), 2007.
 *     http://research.microsoft.com/en-us/um/people/darkok/papers/TOPLAS.pdf
 */

#define	DISFILTER_BLOCK	(32768)
#define	DISFILTERED	1
#define	ORIGSIZE	2
#define	NORMAL_HDR	(1 + 2)
#define	EXTENDED_HDR	(1 + 2 + 2)
// Dispack min reduction should be 8%, otherwise we abort
#define	DIS_MIN_REDUCE	(2622)

#define	MAXINSTR 15     // maximum size of a single instruction in bytes (actually, decodeable ones are shorter)

enum Opcodes
{
  // 1-byte opcodes of special interest (for one reason or another)
  OP_2BYTE  = 0x0f,     // start of 2-byte opcode
  OP_OSIZE  = 0x66,     // operand size prefix
  OP_CALLF  = 0x9a,
  OP_RETNI  = 0xc2,     // ret near+immediate
  OP_RETN   = 0xc3,
  OP_ENTER  = 0xc8,
  OP_INT3   = 0xcc,
  OP_INTO   = 0xce,
  OP_CALLN  = 0xe8,
  OP_JMPF   = 0xea,
  OP_ICEBP  = 0xf1,

  // escape codes we use (these need to be 1-byte opcodes without an address or immediate operand!)
  ESCAPE = OP_ICEBP,
  JUMPTAB = OP_INTO
};

// formats
enum InstructionFormat
{
  // encoding mode
  fNM = 0x0,      // no ModRM
  fAM = 0x1,      // no ModRM, "address mode" (jumps or direct addresses)
  fMR = 0x2,      // ModRM present
  fMEXTRA = 0x3,  // ModRM present, includes extra bits for opcode
  fMODE = 0x3,    // bitmask for mode

  // no ModRM: size of immediate operand
  fNI = 0x0,      // no immediate
  fBI = 0x4,      // byte immediate
  fWI = 0x8,      // word immediate
  fDI = 0xc,      // dword immediate
  fTYPE = 0xc,    // type mask

  // address mode: type of address operand
  fAD = 0x0,      // absolute address
  fDA = 0x4,      // dword absolute jump target
  fBR = 0x8,      // byte relative jump target
  fDR = 0xc,      // dword relative jump target

  // others
  fERR = 0xf      // denotes invalid opcodes
};

enum Streams
{
  ST_OP,                    // prefixes, first byte of opcode
  ST_SIB,                   // SIB byte
  ST_CALL_IDX,              // call table index
  ST_DISP8_R0,              // byte displacement on ModRM, reg no. 0 and following
  ST_DISP8_R1, ST_DISP8_R2, ST_DISP8_R3, ST_DISP8_R4, ST_DISP8_R5, ST_DISP8_R6, ST_DISP8_R7,
  ST_JUMP8,                 // short jump
  ST_IMM8,                  // 8-bit immediate
  ST_IMM16,                 // 16-bit immediate
  ST_IMM32,                 // 32-bit immediate
  ST_DISP32,                // 32-bit displacement
  ST_ADDR32,                // 32-bit direct address
  ST_CALL32,                // 32-bit call target
  ST_JUMP32,                // 32-bit jump target

  ST_MAX,

  // these components of the instruction stream are also identified
  // seperately, but stored together with another stream since there's
  // high correlation between them (or just because one streams provides
  // good context to predict the other)
  ST_MODRM = ST_OP,         // ModRM byte
  ST_OP2 = ST_OP,           // second byte of opcode
  ST_AJUMP32 = ST_JUMP32,   // absolute jump target
  ST_JUMPTBL_COUNT = ST_OP
};

/****************************************************************************/

// These helper functions assume that this code is being compiled on a
// little-endian platform with no alignment restrictions on data accesses.
// If this isn't a safe assumption, change these functions appropriately.
// All byte order dependent operations end up calling them.
//
// I also use the VC++ _byteswap intrinsics to implement big endian stores;
// if your compiler doesn't have them, it should be trivial to get rid of them.

static inline sU8 Load8(const sU8 *s)       { return *s; }
static inline sU16 Load16(const sU8 *s)     { return *((const sU16 *) s); }
static inline sU16 Load16B(const sU8 *s)    { return _byteswap_ushort(Load16(s)); }
static inline sU32 Load32(const sU8 *s)     { return *((const sU32 *) s); }
static inline sU32 Load32B(const sU8 *s)    { return _byteswap_ulong(Load32(s)); }

static inline void Store8(sU8 *d,sU8 v)     { *d = v; }
static inline void Store16(sU8 *d,sU16 v)   { *((sU16 *) d) = v; }
static inline void Store16B(sU8 *d,sU16 v)  { *((sU16 *) d) = _byteswap_ushort(v); }
static inline void Store32(sU8 *d,sU32 v)   { *((sU32 *) d) = v; }
static inline void Store32B(sU8 *d,sU32 v)  { *((sU32 *) d) = _byteswap_ulong(v); }

static inline sU8 Fetch8(sU8 *&s)           { return *s++; }
static inline sU16 Fetch16(sU8 *&s)         { sU16 v = Load16(s);   s += 2; return v; }
static inline sU16 Fetch16B(sU8 *&s)        { sU16 v = Load16B(s);  s += 2; return v; }
static inline sU32 Fetch32(sU8 *&s)         { sU32 v = Load32(s);   s += 4; return v; }
static inline sU32 Fetch32B(sU8 *&s)        { sU32 v = Load32B(s);  s += 4; return v; }

static inline sU8  Write8(sU8 *&d,sU8 v)    { Store8(d,v);  d += 1; return v; }
static inline sU16 Write16(sU8 *&d,sU16 v)  { Store16(d,v); d += 2; return v; }
static inline sU32 Write32(sU8 *&d,sU32 v)  { Store32(d,v); d += 4; return v; }

/****************************************************************************/

static sU32 MoveToFront(sU32 *table,sInt pos,sU32 val)
{
  for(;pos > 0;pos--)
    table[pos] = table[pos-1];

  table[0] = val;
  return val;
}

static inline void AddMTF(sU32 *mtf,sU32 val)
{
  MoveToFront(mtf,255,val);
}

static sInt FindMTF(sU32 *mtf,sU32 val)
{
  for(sInt i=0;i<255;i++)
  {
    if(mtf[i] == val)
    {
      MoveToFront(mtf,i,val);
      return i;
    }
  }

  AddMTF(mtf,val);
  return -1;
}

/****************************************************************************/

struct DataBuffer
{
  sInt Size,Max;
  sU8 *Data;

  DataBuffer()
  {
    Max = 256;
    Data = (sU8 *) malloc(Max);
    ResetBuffer();
  }

  void ResetBuffer()
  {
    Size = 0;
  }

  ~DataBuffer()
  {
    free(Data);
  }

  sU8 *Add(sInt bytes)
  {
    if(Size+bytes>Max)
    {
      Max = (Max*2 < Size+bytes) ? Size+bytes : Max*2;
      Data = (sU8 *) realloc(Data,Max);
    }

    sU8 *ret = Data+Size;
    Size += bytes;
    return ret;
  }
};

/****************************************************************************/

// 1-byte opcodes
sU8 Table1[256] =
{
  // 0       1       2       3       4       5       6       7       8       9       a       b       c       d       e       f
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI, // 0
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI, // 1
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI, // 2
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI, // 3

  fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI, // 4
  fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI, // 5
  fNM|fNI,fNM|fNI,fMR|fNI,fMR|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fDI,fMR|fDI,fNM|fBI,fMR|fBI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI, // 6
  fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR, // 7

  fMR|fBI,fMR|fDI,fMR|fBI,fMR|fBI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // 8
  fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fAM|fDA,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI, // 9
  fAM|fAD,fAM|fAD,fAM|fAD,fAM|fAD,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fBI,fNM|fDI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI, // a
  fNM|fBI,fNM|fBI,fNM|fBI,fNM|fBI,fNM|fBI,fNM|fBI,fNM|fBI,fNM|fBI,fNM|fDI,fNM|fDI,fNM|fDI,fNM|fDI,fNM|fDI,fNM|fDI,fNM|fDI,fNM|fDI, // b

  fMR|fBI,fMR|fBI,fNM|fWI,fNM|fNI,fMR|fNI,fMR|fNI,fMR|fBI,fMR|fDI,fNM|fBI,fNM|fNI,fNM|fWI,fNM|fNI,fNM|fNI,fNM|fBI,fERR   ,fNM|fNI, // c
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fBI,fNM|fBI,fNM|fNI,fNM|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // d
  fAM|fBR,fAM|fBR,fAM|fBR,fAM|fBR,fNM|fBI,fNM|fBI,fNM|fBI,fNM|fBI,fAM|fDR,fAM|fDR,fAM|fAD,fAM|fBR,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI, // e
  fNM|fNI,fERR   ,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fMEXTRA,fMEXTRA,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fMEXTRA,fMEXTRA, // f
};

/****************************************************************************/

// 2-byte opcodes
sU8 Table2[256] =
{
  // 0       1       2       3       4       5       6       7       8       9       a       b       c       d       e       f
  fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fNM|fNI,fERR   ,fNM|fNI,fNM|fNI,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   , // 0
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   , // 1
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fERR   ,fERR   ,fERR   ,fERR   ,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // 2
  fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fERR   ,fNM|fNI,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   , // 3

  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // 4
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // 5
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // 6
  fMR|fBI,fMR|fBI,fMR|fBI,fMR|fBI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fNI,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fMR|fNI,fMR|fNI, // 7

  fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR,fAM|fDR, // 8
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // 9
  fNM|fNI,fNM|fNI,fNM|fNI,fMR|fNI,fMR|fBI,fMR|fNI,fMR|fNI,fMR|fNI,fERR   ,fERR   ,fERR   ,fMR|fNI,fMR|fBI,fMR|fNI,fERR   ,fMR|fNI, // a
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fERR   ,fERR   ,fERR   ,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // b

  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI,fNM|fNI, // c
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // d
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // e
  fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fERR   , // f
};

/****************************************************************************/

// escape opcodes using ModRM byte to get more variants
sU8 TableX[32] =
{
  // 0       1       2       3       4       5       6       7
  fMR|fBI,fERR   ,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // escapes for 0xf6
  fMR|fDI,fERR   ,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI,fMR|fNI, // escapes for 0xf7
  fMR|fNI,fMR|fNI,fERR   ,fERR   ,fERR   ,fERR   ,fERR   ,fERR   , // escapes for 0xfe
  fMR|fNI,fMR|fNI,fMR|fNI,fERR   ,fMR|fNI,fERR   ,fMR|fNI,fERR   , // escapes for 0xff
};

/****************************************************************************/
/****************************************************************************/

struct DisFilterCtx
{
  DataBuffer Buffer[ST_MAX];
  sU32 FuncTable[256];
  sBool NextIsFunc;

  sU32 CodeStart,CodeEnd;

  DisFilterCtx(sU32 codeStart,sU32 codeEnd)
  {
    ResetCtx(codeStart, codeEnd);
  }

  void ResetCtx(sU32 codeStart,sU32 codeEnd)
  {
    NextIsFunc = sTRUE;
    for(sInt i=0;i<256;i++)
      FuncTable[i] = 0;

    CodeStart = codeStart;
    CodeEnd = codeEnd;
    for (sInt i=0; i<ST_MAX; i++)
      Buffer[i].ResetBuffer();
  }

  sInt DetectJumpTable(sU8 *instr, sU32 addr, sU8 *srcEnd)
  {
    assert(addr < CodeEnd);
    sInt nMax = (CodeEnd - addr) / 4;
    sInt count = 0;

    // Check for overflow
    if (instr + (nMax + 1) * 4 > srcEnd) {
	return (0);
    }

    while(count<nMax)
    {
      sU32 codedAddr = Load32(instr + count*4);
      if(codedAddr >= CodeStart && codedAddr < CodeEnd)
        count++;
      else
        break;
    }

    if(count < 3) // if it's less than 3 entries, it's probably not a jump table.
      count = 0;

    return count;
  }

  sInt ProcessInstr(sU8 *instr, sU32 memory, sU8 *srcEnd)
  {

    if(sInt nJump = DetectJumpTable(instr, memory, srcEnd))
    {
      // probable jump table with nJump entries
      sInt remaining = nJump;

      while(remaining)
      {
        sInt count = (remaining < 256) ? remaining : 256;
        Put8(ST_OP,JUMPTAB);
        Put8(ST_JUMPTBL_COUNT,count-1);

        for(sInt i=0;i<count;i++)
        {
          sU32 target = Fetch32(instr);
          sInt ind = FindMTF(FuncTable,target);
          Put8(ST_CALL_IDX,ind+1);
          if(ind == -1)
            Put32(ST_CALL32,target);
        }

        remaining -= count;
      }

      return nJump*4;
    }

    sU8 *start = instr;
    sInt code = Fetch8(instr);
    sInt code2 = 0;
    sBool o16 = sFALSE;
    sInt flags;

    if(NextIsFunc && code != 0xcc)
    {
      AddMTF(FuncTable,memory);
      NextIsFunc = sFALSE;
    }

    if(code == OP_OSIZE)
    {
      o16 = sTRUE;
      code = Fetch8(instr);
    }

    if(code == OP_2BYTE)
    {
      code2 = Fetch8(instr);
      flags = Table2[code2];
    }
    else
      flags = Table1[code];

    if(code == OP_RETNI || code == OP_RETN || code == OP_INT3) // return. function is going to start next.
      NextIsFunc = sTRUE;

    if(flags == fMEXTRA)
      flags = TableX[((*instr >> 3) & 7) | ((code & 0x01) << 3) | ((code & 0x08) << 1)];

    if(flags != fERR)
    {
      if(o16)
        Put8(ST_OP,OP_OSIZE);

      Put8(ST_OP,code);
      if(code == OP_2BYTE)
        Put8(ST_OP2,code2);

      if(code == OP_CALLF || code == OP_JMPF || code == OP_ENTER)
      {
        // far call/jump have a *48-bit* immediate address. we deal with it here by copying the segment index
        // manually and encoding the rest as a normal 32-bit direct address.
        // similarly, enter has a word operand and a byte operand. again, we code the word here, and
        // deal with the byte later during the normal flow.
        Copy16(ST_IMM16,instr);
      }

      if((flags & fMODE) == fMR)
      {
        sInt modrm = Copy8(ST_MODRM,instr);
        sInt sib = 0;

        if((modrm & 0x07) == 4 && modrm < 0xc0)
          sib = Copy8(ST_SIB,instr);

        if((modrm & 0xc0) == 0x40) // register+byte displacement
          Copy8(ST_DISP8_R0 + (modrm & 0x07),instr);

        if((modrm & 0xc0) == 0x80 || (modrm & 0xc7) == 0x05 || (modrm < 0x40 && (sib & 0x07) == 5))
        {
          // register+dword displacement
          Copy32((modrm & 0xc7) == 0x05 ? ST_ADDR32 : ST_DISP32,instr);
        }
      }

      if((flags & fMODE) == fAM)
      {
        switch(flags & fTYPE)
        {
        case fAD: Copy32(ST_ADDR32,instr);  break;
        case fDA: Copy32(ST_AJUMP32,instr); break;
        case fBR: Copy8(ST_JUMP8,instr);    break;

        case fDR:
          {
            sU32 target = Fetch32(instr);
            target += (instr - start) + memory;
            if(code != OP_CALLN) // not a near call
              Put32(ST_JUMP32,target);
            else
            {
              sInt ind = FindMTF(FuncTable,target);
              Put8(ST_CALL_IDX,ind+1);
              if(ind == -1)
                Put32(ST_CALL32,target);
            }
          }
          break;
        }
      }
      else
      {
        switch(flags & fTYPE)
        {
        case fBI: Copy8(ST_IMM8,instr);   break;
        case fWI: Copy16(ST_IMM16,instr); break;

        case fDI:
          if(!o16)
            Copy32(ST_IMM32,instr);
          else
            Copy16(ST_IMM16,instr);
          break;
        }
      }

      return instr - start;
    }
    else // couldn't decode instruction
    {
      Put8(ST_OP,ESCAPE); // escape code
      Put8(ST_OP,*start); // the unrecognized opcode
      return 1;
    }
  }

  sU8 *Flush(sU8 *out, sU32 &sz)
  {
    sU32 size = 0;

    if (sz < ST_MAX * 16)
      return (NULL);
    size = ST_MAX * 4; // 4 bytes per stream to encode the size
    for(sInt i=0;i<ST_MAX;i++) {
      size += Buffer[i].Size;
      if (size >= sz)  return (NULL); // Check for output overflow
    }

    // Output ptr is supplied by caller
    sU8 *outPtr = out;

    for(sInt i=0;i<ST_MAX;i++)
      Write32(outPtr,Buffer[i].Size);

    for(sInt i=0;i<ST_MAX;i++)
    {
      memcpy(outPtr,Buffer[i].Data,Buffer[i].Size);
      outPtr += Buffer[i].Size;
    }

    assert(outPtr == out + size);
    sz = size;
    return out;
  }

  sU8  Put8(sInt stream,sU8 v)      { Store8  (Buffer[stream].Add(1),v); return v; }
  sU16 Put16(sInt stream,sU16 v)    { Store16B(Buffer[stream].Add(2),v); return v; }
  sU32 Put32(sInt stream,sU32 v)    { Store32B(Buffer[stream].Add(4),v); return v; }

  sU8  Copy8(sInt stream,sU8 *&s)   { return Put8 (stream,Fetch8(s));  }
  sU16 Copy16(sInt stream,sU8 *&s)  { return Put16(stream,Fetch16(s)); }
  sU32 Copy32(sInt stream,sU8 *&s)  { return Put32(stream,Fetch32(s)); }
};

/****************************************************************************/

sU8 *
DisFilter(sU8 *src, sU32 size, sU32 origin, sU8 *dst, sU32 &outputSize)
{
  sU8 *srcEnd = src + size;
  DisFilterCtx ctx(origin,origin+size);

  if (size < MAXINSTR)
	  return (NULL);
  // main loop: handle everything but the last few bytes
  sU32 pos = 0;
  while(pos < size - MAXINSTR)
  {
    sInt bytes = ctx.ProcessInstr(src + pos, origin + pos, srcEnd);
    pos += bytes;
  }

  // for the last few bytes, be very careful not to read past the end
  // of the input instruction stream. create a check point on every
  // instruction; if PackInstr would've read past the end of the input
  // stream, we undo the last step.
  while(pos < size)
  {
    // copy remaining instr bytes into buffer
    sU8 instrBuf[MAXINSTR] = { 0 };
    memcpy(instrBuf,src + pos,size - pos);

    // save current output size for all streams
    sInt checkpt[ST_MAX];
    for(sInt i=0;i<ST_MAX;i++)
      checkpt[i] = ctx.Buffer[i].Size;

    // process the instruction
    sInt bytes = ctx.ProcessInstr(instrBuf, origin + pos, srcEnd);

    if(pos + bytes <= size) // valid instruction
      pos += bytes;
    else
    {
      // we read past the end. restore to checkpoint!
      for(sInt i=0;i<ST_MAX;i++)
        ctx.Buffer[i].Size = checkpt[i];

      break;
    }
  }

  // if there's still bytes left, encode them as escapes.
  while(pos < size)
  {
    ctx.Put8(ST_OP,ESCAPE);
    ctx.Put8(ST_OP,src[pos]);
    pos++;
  }

  return ctx.Flush(dst, outputSize);
}

/****************************************************************************/

static inline sU8 Copy8(sU8 *&d,sU8 *&s)      { sU8 v = Fetch8(s); Write8(d,v); return v; }
static inline sU16 Copy16(sU8 *&d,sU8 *&s)    { sU16 v = Fetch16B(s); Write16(d,v); return v; }
static inline sU32 Copy32(sU8 *&d,sU8 *&s)    { sU32 v = Fetch32B(s); Write32(d,v); return v; }

// some helpers for bounds checking. this really sucks, but I didn't see any
// better way to make this safe...
#define CheckSrc(strm,size)     if(stream[strm]+size > streamEnd[strm]) return sFALSE
#define CheckDst(size)          if(dest+size > destEnd) return sFALSE
#define CheckSrcDst(strm,size)  if(stream[strm]+size > streamEnd[strm] || dest+size > destEnd) return sFALSE

#define Copy8Chk(strm)          do { CheckSrcDst(strm,1); Copy8 (dest,stream[strm]); } while(0)
#define Copy16Chk(strm)         do { CheckSrcDst(strm,2); Copy16(dest,stream[strm]); } while(0)
#define Copy32Chk(strm)         do { CheckSrcDst(strm,4); Copy32(dest,stream[strm]); } while(0)

sBool
DisUnFilter(sU8 *source,sU32 sourceSize,sU8 *dest,sU32 destSize,sU32 memStart)
{
  sU8 *stream[ST_MAX];
  sU8 *streamEnd[ST_MAX];
  sU32 funcTable[256];

  // read header (list of stream sizes)
  if(sourceSize < ST_MAX*4)
    return sFALSE;

  sU8 *hdr = source;
  sU8 *cur = source + ST_MAX*4;
  for(sInt i=0;i<ST_MAX;i++)
  {
    stream[i] = cur;
    cur += Fetch32(hdr);
    streamEnd[i] = cur;
  }

  if(cur != source + sourceSize)
    return sFALSE; // size doesn't make sense

  // start decoding
  for(sInt i=0;i<256;i++)
    funcTable[i] = 0;

  sBool nextIsFunc = sTRUE;

  sU8 *destStart = dest;
  sU8 *destEnd = destStart + destSize;

  while(stream[ST_OP]<streamEnd[ST_OP])
  {
    sU8 *start = dest;
    sU32 memory = memStart + (dest - destStart);

    sInt code = Fetch8(stream[ST_OP]);
    if(code == JUMPTAB) // jump table escape
    {
      CheckSrc(ST_JUMPTBL_COUNT,1);
      sInt count = Fetch8(stream[ST_JUMPTBL_COUNT]) + 1;

      for(sInt i=0;i<count;i++)
      {
        sU32 target;

        CheckSrc(ST_CALL_IDX,1);
        sInt ind = Fetch8(stream[ST_CALL_IDX]);
        if(ind)
          target = MoveToFront(funcTable,ind-1,funcTable[ind-1]);
        else
        {
          CheckSrc(ST_CALL32,4);
          target = Fetch32B(stream[ST_CALL32]);
          AddMTF(funcTable,target);
        }

        CheckDst(4);
        Write32(dest,target);
      }

      continue;
    }

    if(nextIsFunc && code != OP_INT3)
    {
      AddMTF(funcTable,memory);
      nextIsFunc = sFALSE;
    }

    if(code == ESCAPE) // escape
      Copy8Chk(ST_OP);
    else
    {
      CheckDst(1);
      Write8(dest,code);

      sInt flags = 0;
      sBool o16 = sFALSE;
      if(code == OP_OSIZE) // operand size prefix
      {
        o16 = sTRUE;
        CheckSrcDst(ST_OP,1);
        code = Copy8(dest,stream[ST_OP]);
      }

      if(code == OP_RETNI || code == OP_RETN || code == OP_INT3) // return/padding
        nextIsFunc = sTRUE; // next opcode is likely to be first of a new function

      if(code == OP_2BYTE) // two-byte opcode, additional opcode byte follows
      {
        CheckSrcDst(ST_OP2,1);
        flags = Table2[Copy8(dest,stream[ST_OP2])];
      }
      else
        flags = Table1[code];

      assert(flags != fERR);

      if(code == OP_CALLF || code == OP_JMPF || code == OP_ENTER)
      {
        // far call/jump have a *48-bit* immediate address. we deal with it here by copying the segment
        // index manually and encoding the rest as a normal 32-bit direct address.
        // similarly, enter has a word operand and a byte operand. again, we code the word here, and
        // deal with the byte later during the normal flow.
        Copy16Chk(ST_IMM16);
      }

      if(flags & fMR)
      {
        CheckSrcDst(ST_MODRM,1);
        sInt modrm = Copy8(dest,stream[ST_MODRM]);
        sInt sib = 0;

        if(flags == fMEXTRA)
          flags = TableX[((modrm >> 3) & 7) | ((code & 0x01) << 3) | ((code & 0x08) << 1)];

        if((modrm & 0x07) == 4 && modrm < 0xc0)
        {
          CheckSrcDst(ST_SIB,1);
          sib = Copy8(dest,stream[ST_SIB]);
        }

        if((modrm & 0xc0) == 0x40) // register+byte displacement
        {
          sInt st = (modrm & 0x07) + ST_DISP8_R0;
          Copy8Chk(st);
        }

        if((modrm & 0xc0) == 0x80 || (modrm & 0xc7) == 0x05 || (modrm < 0x40 && (sib & 0x07) == 0x05))
        {
          sInt st = (modrm & 0xc7) == 5 ? ST_ADDR32 : ST_DISP32;
          Copy32Chk(st);
        }
      }

      if((flags & fMODE) == fAM)
      {
        switch(flags & fTYPE)
        {
        case fAD: Copy32Chk(ST_ADDR32);   break;
        case fDA: Copy32Chk(ST_AJUMP32);  break;
        case fBR: Copy8Chk(ST_JUMP8);     break;

        case fDR:
          {
            sU32 target;
            if(code == OP_CALLN)
            {
              CheckSrc(ST_CALL_IDX,1);
              sInt ind = Fetch8(stream[ST_CALL_IDX]);
              if(ind)
                target = MoveToFront(funcTable,ind-1,funcTable[ind-1]);
              else
              {
                CheckSrc(ST_CALL32,4);
                target = Fetch32B(stream[ST_CALL32]);
                AddMTF(funcTable,target);
              }
            }
            else
            {
              CheckSrc(ST_JUMP32,4);
              target = Fetch32B(stream[ST_JUMP32]);
            }

            target -= (dest - start) + 4 + memory;
            CheckDst(4);
            Write32(dest,target);
          }
          break;
        }
      }
      else
      {
        switch(flags & fTYPE)
        {
        case fBI: Copy8Chk(ST_IMM8);    break;
        case fWI: Copy16Chk(ST_IMM16);  break;

        case fDI:
          if(!o16)
            Copy32Chk(ST_IMM32);
          else
            Copy16Chk(ST_IMM16);
          break;
        }
      }
    }
  }

  return sTRUE;
}

/*
 * NOTE: function unused. Retained for future need.
 */
#if 0
/*
 * Try to estimate if the given data block contains 32-bit x86 instructions
 * especially of the call and jmp variety.
 * Estimator is adapted from CSC 3.2 Analyzer (Fu Siyuan).
 */
static int
is_x86_code(uchar_t *buf, int len)
{
	uint32_t avgFreq, freq[256] = {0};
	uint32_t freq0x80[2] = {0};
	uint32_t ln = len;
	int i;

	for (i = 0; i < len; i++) {
		freq[buf[i]]++;
	}

	for (i = 0; i< 256; i++) {
		freq0x80[i>>7] += freq[i];
	}

	avgFreq = ln>>8;
	return (freq[0x8b] > avgFreq && freq[0x00] > avgFreq * 2 && freq[0xE8] > 6);
}
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * E8 E9 Call/Jmp transform routines. Convert relative Call and Jmp addresses
 * to absolute values to improve compression. A couple of tricks are employed:
 * 1) Avoid transforming zero adresses or where adding the current offset to
 *    to the presumed address results in a zero result. This avoids a bunch of
 *    false positives.
 * 2) Store transformed values in big-endian format. This improves compression.
 */
int
Forward_E89(uint8_t *src, uint64_t sz)
{
	uint32_t i;
	uint32_t size, conversions;

	if (sz > UINT32_MAX || sz < 25) {
		return (-1);
	}

	size = sz;
	i = 0;
	conversions = 0;
	while (i < size-4) {
		if ((src[i] & 0xfe) == 0xe8 &&
		    (src[i+4] == 0 || src[i+4] == 0xff))
		{
			uint32_t off;

			off = (src[i+1] | (src[i+2] << 8) | (src[i+3] << 16));
			if (off > 0) {
				off += i;
				off &= 0xffffff;
				if (off > 0) {
					src[i+1] = (uint8_t)(off >> 16);
					src[i+2] = (uint8_t)(off >> 8);
					src[i+3] = (uint8_t)off;
					conversions++;
				}
			}
		}
		i++;
	}
	if (conversions < 10)
		return (-1);
	return (0);
}

int
Inverse_E89(uint8_t *src, uint64_t sz)
{
	uint32_t i;
	uint32_t size;

	if (sz > UINT32_MAX) {
		return (-1);
	}

	size = sz;
	i = size-5;;
	while (i > 0) {
		if ((src[i] & 0xfe) == 0xe8 &&
		    (src[i+4] == 0 || src[i+4] == 0xff))
		{
			uint32_t val;

			val = (src[i+3] | (src[i+2] << 8) | (src[i+1] << 16));
			if (val > 0) {
				val -= i;
				val &= 0xffffff;
				if (val > 0) {
					src[i+1] = (uint8_t)val;
					src[i+2] = (uint8_t)(val >> 8);
					src[i+3] = (uint8_t)(val >> 16);
				}
			}
		}
		i--;
	}
	return (0);
}

/*
 * NOTE: function unused. Retained for future need.
 */
#if 0
/*
 * 32-bit x86 executable packer top-level routines. Detected x86 executable data
 * are passed through these encoding routines. The data chunk is split into 32KB
 * blocks and each block is separately Dispack-ed. The code tries to detect if
 * a block contains valid x86 code by trying to estimate some instruction metrics.
 */
int
dispack_encode(uchar_t *from, uint64_t fromlen, uchar_t *to, uint64_t *dstlen)
{
	uchar_t *pos, *hdr, type, *pos_to, *to_last;
	sU32 len;
#ifdef	DEBUG_STATS
	double strt, en;
#endif

	if (fromlen > UINT32_MAX)
		return (-1);

	if (fromlen < DISFILTER_BLOCK)
		return (-1);

#ifdef	DEBUG_STATS
	strt = get_wtime_millis();
#endif
	pos = from;
	len = (sU32)fromlen;
	pos_to = to;
	to_last = to + *dstlen;
	while (len > 0) {
		DisFilterCtx ctx(0, DISFILTER_BLOCK);
		sU32 sz;
		sU16 origsize;
		sU32 out;
		sU8 *rv;

		if (len > DISFILTER_BLOCK)
			sz = DISFILTER_BLOCK;
		else
			sz = len;

		hdr = pos_to;
		type = 0;
		origsize = sz;
		if (sz < DISFILTER_BLOCK) {
			type |= ORIGSIZE;
			pos_to += EXTENDED_HDR;
			U16_P(hdr + NORMAL_HDR) = LE16(origsize);
		} else {
			pos_to += NORMAL_HDR;
		}

		out = sz;
		if (is_x86_code(pos, sz)) {
			ctx.ResetCtx(0, sz);
			rv = DisFilter(ctx, pos, sz, 0, pos_to, out);
		} else {
			rv = NULL;
		}
		if (rv != pos_to || sz == out) {
			if (pos_to + origsize >= to_last) {
				return (-1);
			}
			memcpy(pos_to, pos, origsize);
			*hdr = type;
			hdr++;
			U16_P(hdr) = LE16(origsize);
			pos_to += origsize;
		} else {
			sU16 csize;

			if (pos_to + out >= to_last) {
				return (-1);
			}
			type |= DISFILTERED;
			*hdr = type;
			hdr++;
			csize = out;
			U16_P(hdr) = LE16(csize);
			pos_to += csize;
		}
		pos += sz;
		len -= sz;
	}
	*dstlen = pos_to - to;
#ifdef	DEBUG_STATS
	en = get_wtime_millis();
	cerr << "Dispack: Processed at " << get_mb_s(fromlen, strt, en) << " MB/s" << endl;
#endif
	if ((fromlen - *dstlen) < DIS_MIN_REDUCE) {
#ifdef	DEBUG_STATS
		cerr << "Dispack: Failed, reduction too less" << endl;
#endif
		return (-1);
	}
#ifdef	DEBUG_STATS
	cerr << "Dispack: srclen: " << fromlen << ", dstlen: " << *dstlen << endl;
#endif
	return (0);
}
#endif

/*
 * This function retained for ability to decode older archives encoded using raw block
 * dispack.
 */
int
dispack_decode(uchar_t *from, uint64_t fromlen, uchar_t *to, uint64_t *dstlen)
{
	uchar_t *pos, type, *pos_to, *to_last;
	uint64_t len;

	pos = from;
	len = fromlen;
	pos_to = to;
	to_last = to + *dstlen;
	while (len > 0) {
		sU32 sz, cmpsz;

		type = *pos++;
		len--;
		sz = DISFILTER_BLOCK;
		cmpsz = LE16(U16_P(pos));
		pos += 2;
		len -= 2;
		if (type & ORIGSIZE) {
			sz = LE16(U16_P(pos));
			pos += 2;
			len -= 2;
		}

		if (type & DISFILTERED) {
			if (pos_to + sz > to_last) {
				return (-1);
			}
			if (DisUnFilter(pos, cmpsz, pos_to, sz, 0) != sTRUE) {
				return (-1);
			}
			pos += cmpsz;
			pos_to += sz;
			len -= cmpsz;
		} else {
			if (pos_to + cmpsz > to_last) {
				return (-1);
			}
			memcpy(pos_to, pos, cmpsz);

			/*
			 * If E8E9 was applied on this block, apply the inverse transform.
			 * This only happens if this block was detected as x86 instruction
			 * stream and Dispack was tried but it failed.
			 */
			pos += cmpsz;
			pos_to += cmpsz;
			len -= cmpsz;
		}
	}
	*dstlen = pos_to - to;
	return (0);
}

#ifdef	__cplusplus
}
#endif

