/*-----------------------------------------------------------*/
/* Block Sorting, Lossless Data Compression Library.         */
/* Lempel Ziv Prediction                                     */
/*-----------------------------------------------------------*/

/*--

This file is a part of bsc and/or libbsc, a program and a library for
lossless, block-sorting data compression.

Copyright (c) 2009-2012 Ilya Grebnov <ilya.grebnov@gmail.com>
Copyright (c) 2012 Moinak Ghosh <moinakg@gmail.com>

See file AUTHORS for a full list of contributors.

The bsc and libbsc is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The bsc and libbsc is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the bsc and libbsc. If not, see http://www.gnu.org/licenses/.

Please see the files COPYING and COPYING.LIB for full copyright information.

See also the bsc and libbsc web site:
  http://libbsc.com/ for more information.

--*/

/*
 *  TODO: Port the parallel implementation.
 */
#undef LZP_OPENMP

#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <allocator.h>
#include <sys/types.h>

#include "lzp.h"

#define LZP_MATCH_FLAG 	0xf2

static
inline int bsc_lzp_num_blocks(ssize_t n)
{
    int nb;

    if (n <       256 * 1024)   return 1;
    if (n <  4 * 1024 * 1024)   return 2;
    if (n < 16 * 1024 * 1024)   return 4;
    if (n <    LZP_MAX_BLOCK)   return 8;

    nb = n / LZP_MAX_BLOCK;
    if (n % LZP_MAX_BLOCK) nb++;
    return (nb);
}

static
int bsc_lzp_encode_block(const unsigned char * input, const unsigned char * inputEnd, unsigned char * output, unsigned char * outputEnd, int hashSize, int minLen)
{
    int *lookup, i;
    if (inputEnd - input < 16)
    {
        return LZP_NOT_COMPRESSIBLE;
    }

    if (lookup = (int *)slab_calloc(NULL, (int)(1 << hashSize), sizeof(int)))
    {
        unsigned int            mask        = (int)(1 << hashSize) - 1;
        const unsigned char *   inputStart  = input;
        const unsigned char *   outputStart = output;
        const unsigned char *   outputEOB   = outputEnd - 4;

        unsigned int context = 0;
        for (i = 0; i < 4; ++i)
        {
            context = (context << 8) | (*output++ = *input++);
        }

        const unsigned char * heuristic      = input;
        const unsigned char * inputMinLenEnd = inputEnd - minLen - 8;
        while ((input < inputMinLenEnd) && (output < outputEOB))
        {
            unsigned int index = ((context >> 15) ^ context ^ (context >> 3)) & mask;
            int value = lookup[index]; lookup[index] = (int)(input - inputStart);
            if (value > 0)
            {
                const unsigned char * reference = inputStart + value;
                if ((*(unsigned int *)(input + minLen - 4) == *(unsigned int *)(reference + minLen - 4)) && (*(unsigned int *)(input) == *(unsigned int *)(reference)))
                {
                    if ((heuristic > input) && (*(unsigned int *)heuristic != *(unsigned int *)(reference + (heuristic - input))))
                    {
                        goto LZP_MATCH_NOT_FOUND;
                    }

                    int len = 4;
                    for (; input + len < inputMinLenEnd; len += 4)
                    {
                        if (*(unsigned int *)(input + len) != *(unsigned int *)(reference + len)) break;
                    }
                    if (len < minLen)
                    {
                        if (heuristic < input + len) heuristic = input + len;
                        goto LZP_MATCH_NOT_FOUND;
                    }

                    if (input[len] == reference[len]) len++;
                    if (input[len] == reference[len]) len++;
                    if (input[len] == reference[len]) len++;

                    input += len; context = input[-1] | (input[-2] << 8) | (input[-3] << 16) | (input[-4] << 24);

                    *output++ = LZP_MATCH_FLAG;

                    len -= minLen; while (len >= 254) { len -= 254; *output++ = 254; if (output >= outputEOB) break; }

                    *output++ = (unsigned char)(len);
                }
                else
                {
		    unsigned char next;
LZP_MATCH_NOT_FOUND:
                    next = *output++ = *input++; context = (context << 8) | next;
                    if (next == LZP_MATCH_FLAG) *output++ = 255;
                }
            }
            else
            {
                context = (context << 8) | (*output++ = *input++);
            }
        }

        while ((input < inputEnd) && (output < outputEOB))
        {
            unsigned int index = ((context >> 15) ^ context ^ (context >> 3)) & mask;
            int value = lookup[index]; lookup[index] = (int)(input - inputStart);
            if (value > 0)
            {
                unsigned char next = *output++ = *input++; context = (context << 8) | next;
                if (next == LZP_MATCH_FLAG) *output++ = 255;
            }
            else
            {
                context = (context << 8) | (*output++ = *input++);
            }
        }

        slab_free(NULL, lookup);

        return (output >= outputEOB) ? LZP_NOT_COMPRESSIBLE : (int)(output - outputStart);
    }

    return LZP_NOT_ENOUGH_MEMORY;
}

static
int bsc_lzp_decode_block(const unsigned char * input, const unsigned char * inputEnd, unsigned char * output, int hashSize, int minLen)
{
    int *lookup, i;
    if (inputEnd - input < 4)
    {
        return LZP_UNEXPECTED_EOB;
    }

    if (lookup = (int *)slab_calloc(NULL, (int)(1 << hashSize), sizeof(int)))
    {
        unsigned int            mask        = (int)(1 << hashSize) - 1;
        const unsigned char *   outputStart = output;

        unsigned int context = 0;
        for (i = 0; i < 4; ++i)
        {
            context = (context << 8) | (*output++ = *input++);
        }

        while (input < inputEnd)
        {
            unsigned int index = ((context >> 15) ^ context ^ (context >> 3)) & mask;
            int value = lookup[index]; lookup[index] = (int)(output - outputStart);
            if (*input == LZP_MATCH_FLAG && value > 0)
            {
                input++;
                if (*input != 255)
                {
                    int len = minLen; while (1) { len += *input; if (*input++ != 254) break; }

                    const unsigned char * reference = outputStart + value;
                          unsigned char * outputEnd = output + len;

                    if (output - reference < 4)
                    {
                        int offset[4] = {0, 3, 2, 3};

                        *output++ = *reference++;
                        *output++ = *reference++;
                        *output++ = *reference++;
                        *output++ = *reference++;

                        reference -= offset[output - reference];
                    }

                    while (output < outputEnd) { *(unsigned int *)output = *(unsigned int*)reference; output += 4; reference += 4; }

                    output = outputEnd; context = output[-1] | (output[-2] << 8) | (output[-3] << 16) | (output[-4] << 24);
                }
                else
                {
                    input++; context = (context << 8) | (*output++ = LZP_MATCH_FLAG);
                }
            }
            else
            {
                context = (context << 8) | (*output++ = *input++);
            }
        }

        slab_free(NULL, lookup);

        return (int)(output - outputStart);
    }

    return LZP_NOT_ENOUGH_MEMORY;
}

static
ssize_t bsc_lzp_compress_serial(const unsigned char * input, unsigned char * output, ssize_t n, int hashSize, int minLen)
{
    if (bsc_lzp_num_blocks(n) == 1)
    {
        int result = bsc_lzp_encode_block(input, input + n, output + 1, output + n - 1, hashSize, minLen);
        if (result >= LZP_NO_ERROR) result = (output[0] = 1, result + 1);

        return result;
    }

    int nBlocks   = bsc_lzp_num_blocks(n);
    int chunkSize;
    int blockId;
    ssize_t outputPtr = 1 + 8 * nBlocks;

    if (n > LZP_MAX_BLOCK)
        chunkSize = LZP_MAX_BLOCK;
    else
        chunkSize = n / nBlocks;

    output[0] = nBlocks;
    for (blockId = 0; blockId < nBlocks; ++blockId)
    {
        ssize_t inputStart  = blockId * chunkSize;
        int inputSize   = blockId != nBlocks - 1 ? chunkSize : n - inputStart;
        int outputSize  = inputSize; if (outputSize > n - outputPtr) outputSize = n - outputPtr;

        int result = bsc_lzp_encode_block(input + inputStart, input + inputStart + inputSize, output + outputPtr, output + outputPtr + outputSize, hashSize, minLen);
        if (result < LZP_NO_ERROR)
        {
            if (outputPtr + inputSize >= n) return LZP_NOT_COMPRESSIBLE;
            result = inputSize; memcpy(output + outputPtr, input + inputStart, inputSize);
        }

        *(int *)(output + 1 + 8 * blockId + 0) = inputSize;
        *(int *)(output + 1 + 8 * blockId + 4) = result;

        outputPtr += result;
    }

    return outputPtr;
}

#ifdef LZP_OPENMP

static
int bsc_lzp_compress_parallel(const unsigned char * input, unsigned char * output, ssize_t n, int hashSize, int minLen)
{
    if (unsigned char * buffer = (unsigned char *)bsc_malloc(n * sizeof(unsigned char)))
    {
        int compressionResult[ALPHABET_SIZE];

        int nBlocks   = bsc_lzp_num_blocks(n);
        int result    = LZP_NO_ERROR;
        int chunkSize = n / nBlocks;

        int numThreads = omp_get_max_threads();
        if (numThreads > nBlocks) numThreads = nBlocks;

        output[0] = nBlocks;
        #pragma omp parallel num_threads(numThreads) if(numThreads > 1)
        {
            if (omp_get_num_threads() == 1)
            {
                result = bsc_lzp_compress_serial(input, output, n, hashSize, minLen);
            }
            else
            {
                #pragma omp for schedule(dynamic)
                for (int blockId = 0; blockId < nBlocks; ++blockId)
                {
                    int blockStart   = blockId * chunkSize;
                    int blockSize    = blockId != nBlocks - 1 ? chunkSize : n - blockStart;

                    compressionResult[blockId] = bsc_lzp_encode_block(input + blockStart, input + blockStart + blockSize, buffer + blockStart, buffer + blockStart + blockSize, hashSize, minLen);
                    if (compressionResult[blockId] < LZP_NO_ERROR) compressionResult[blockId] = blockSize;

                    *(int *)(output + 1 + 8 * blockId + 0) = blockSize;
                    *(int *)(output + 1 + 8 * blockId + 4) = compressionResult[blockId];
                }

                #pragma omp single
                {
                    result = 1 + 8 * nBlocks;
                    for (int blockId = 0; blockId < nBlocks; ++blockId)
                    {
                        result += compressionResult[blockId];
                    }

                    if (result >= n) result = LZP_NOT_COMPRESSIBLE;
                }

                if (result >= LZP_NO_ERROR)
                {
                    #pragma omp for schedule(dynamic)
                    for (int blockId = 0; blockId < nBlocks; ++blockId)
                    {
                        int blockStart   = blockId * chunkSize;
                        int blockSize    = blockId != nBlocks - 1 ? chunkSize : n - blockStart;

                        int outputPtr = 1 + 8 * nBlocks;
                        for (int p = 0; p < blockId; ++p) outputPtr += compressionResult[p];

                        if (compressionResult[blockId] != blockSize)
                        {
                            memcpy(output + outputPtr, buffer + blockStart, compressionResult[blockId]);
                        }
                        else
                        {
                            memcpy(output + outputPtr, input + blockStart, compressionResult[blockId]);
                        }
                    }
                }
            }
        }

        bsc_free(buffer);

        return result;
    }
    return LZP_NOT_ENOUGH_MEMORY;
}

#endif

ssize_t lzp_compress(const unsigned char * input, unsigned char * output, ssize_t n, int hashSize, int minLen, int features)
{

#ifdef LZP_OPENMP

    if ((bsc_lzp_num_blocks(n) != 1) && (features & LZP_FEATURE_MULTITHREADING))
    {
        return bsc_lzp_compress_parallel(input, output, n, hashSize, minLen);
    }

#endif

    return bsc_lzp_compress_serial(input, output, n, hashSize, minLen);
}

ssize_t lzp_decompress(const unsigned char * input, unsigned char * output, ssize_t n, int hashSize, int minLen, int features)
{
    int nBlocks = input[0];

    if (nBlocks == 1)
    {
        return bsc_lzp_decode_block(input + 1, input + n, output, hashSize, minLen);
    }

    int decompressionResult[ALPHABET_SIZE];

#ifdef LZP_OPENMP

    if (features & LZP_FEATURE_MULTITHREADING)
    {
        #pragma omp parallel for schedule(dynamic)
        for (int blockId = 0; blockId < nBlocks; ++blockId)
        {
            int inputPtr = 0;  for (int p = 0; p < blockId; ++p) inputPtr  += *(int *)(input + 1 + 8 * p + 4);
            int outputPtr = 0; for (int p = 0; p < blockId; ++p) outputPtr += *(int *)(input + 1 + 8 * p + 0);

            inputPtr += 1 + 8 * nBlocks;

            int inputSize  = *(int *)(input + 1 + 8 * blockId + 4);
            int outputSize = *(int *)(input + 1 + 8 * blockId + 0);

            if (inputSize != outputSize)
            {
                decompressionResult[blockId] = bsc_lzp_decode_block(input + inputPtr, input + inputPtr + inputSize, output + outputPtr, hashSize, minLen);
            }
            else
            {
                decompressionResult[blockId] = inputSize; memcpy(output + outputPtr, input + inputPtr, inputSize);
            }
        }
    }
    else

#endif

    {
	int blockId, p;

        for (blockId = 0; blockId < nBlocks; ++blockId)
        {
            ssize_t inputPtr = 0;  for (p = 0; p < blockId; ++p) inputPtr  += *(int *)(input + 1 + 8 * p + 4);
            ssize_t outputPtr = 0; for (p = 0; p < blockId; ++p) outputPtr += *(int *)(input + 1 + 8 * p + 0);

            inputPtr += 1 + 8 * nBlocks;

            int inputSize  = *(int *)(input + 1 + 8 * blockId + 4);
            int outputSize = *(int *)(input + 1 + 8 * blockId + 0);

            if (inputSize != outputSize)
            {
                decompressionResult[blockId] = bsc_lzp_decode_block(input + inputPtr, input + inputPtr + inputSize, output + outputPtr, hashSize, minLen);
            }
            else
            {
                decompressionResult[blockId] = inputSize; memcpy(output + outputPtr, input + inputPtr, inputSize);
            }
        }
    }

    ssize_t dataSize = 0;
    int result = LZP_NO_ERROR;
    int blockId;
    for (blockId = 0; blockId < nBlocks; ++blockId)
    {
        if (decompressionResult[blockId] < LZP_NO_ERROR) result = decompressionResult[blockId];
        dataSize += decompressionResult[blockId];
    }

    return (result == LZP_NO_ERROR) ? dataSize : result;
}

/*
 * Counter-intuitively we use a larger hash (with better LZP compression) for lower global
 * compression levels. So that LZP preprocessing plays along nicely with the primary
 * compression algorithm being used and actually provides a benefit.
 */
int lzp_hash_size(int level) {
    if (level > 7) {
        return (LZP_DEFAULT_LZPHASHSIZE + 2);
    } else if (level > 5) {
        return (LZP_DEFAULT_LZPHASHSIZE + 3);
    } else if (level > 3) {
        return (LZP_DEFAULT_LZPHASHSIZE + 4);
    } else {
        return (LZP_DEFAULT_LZPHASHSIZE + 5);
    }
}
/*-----------------------------------------------------------*/
/* End                                               lzp.cpp */
/*-----------------------------------------------------------*/
