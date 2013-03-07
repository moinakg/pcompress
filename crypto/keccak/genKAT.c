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
The Keccak sponge function, designed by Guido Bertoni, Joan Daemen,
Michaël Peeters and Gilles Van Assche. For more information, feedback or
questions, please refer to our website: http://keccak.noekeon.org/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "KeccakDuplex.h"
#include "KeccakNISTInterface.h"
#include "KeccakSponge.h"

#define MAX_MARKER_LEN      50
#define SUBMITTER_INFO_LEN  128

typedef enum { KAT_SUCCESS = 0, KAT_FILE_OPEN_ERROR = 1, KAT_HEADER_ERROR = 2, KAT_DATA_ERROR = 3, KAT_HASH_ERROR = 4 } STATUS_CODES;

#define AllowExtendedFunctions
#define ExcludeExtremelyLong

#ifdef AllowExtendedFunctions
#define SqueezingOutputLength 4096
#endif

STATUS_CODES    genShortMsg(int hashbitlen);
STATUS_CODES    genLongMsg(int hashbitlen);
STATUS_CODES    genExtremelyLongMsg(int hashbitlen);
STATUS_CODES    genMonteCarlo(int hashbitlen);
#ifdef AllowExtendedFunctions
STATUS_CODES    genMonteCarloSqueezing(int hashbitlen);
STATUS_CODES    genShortMsgSponge(unsigned int rate, unsigned int capacity, int outputLength, const char *fileName);
STATUS_CODES    genDuplexKAT(unsigned int rate, unsigned int capacity, const char *fileName);
#endif
int     FindMarker(FILE *infile, const char *marker);
int     ReadHex(FILE *infile, BitSequence *A, int Length, const char *str);
void    fprintBstr(FILE *fp, const char *S, BitSequence *A, int L);


STATUS_CODES
genKAT_main()
{
    int     i, ret_val,  bitlens[4] = { 224, 256, 384, 512 };

#ifdef AllowExtendedFunctions
    if ( (ret_val = genShortMsgSponge(1024, 576, 4096, "ShortMsgKAT_0.txt")) != KAT_SUCCESS )
        return (STATUS_CODES)ret_val;
    if ( (ret_val = genLongMsg(0)) != KAT_SUCCESS )
        return (STATUS_CODES)ret_val;
#ifndef ExcludeExtremelyLong
    if ( (ret_val = genExtremelyLongMsg(0)) != KAT_SUCCESS )
        return (STATUS_CODES)ret_val;
#endif
    if ( (ret_val = genMonteCarloSqueezing(0)) != KAT_SUCCESS )
        return (STATUS_CODES)ret_val;
#endif

    for ( i=0; i<4; i++ ) {
        if ( (ret_val = genShortMsg(bitlens[i])) != KAT_SUCCESS )
            return (STATUS_CODES)ret_val;
        if ( (ret_val = genLongMsg(bitlens[i])) != KAT_SUCCESS )
            return (STATUS_CODES)ret_val;
#ifndef ExcludeExtremelyLong
        if ( (ret_val = genExtremelyLongMsg(bitlens[i])) != KAT_SUCCESS )
            return (STATUS_CODES)ret_val;
#endif
        if ( (ret_val = genMonteCarlo(bitlens[i])) != KAT_SUCCESS )
            return (STATUS_CODES)ret_val;
    }

#ifdef AllowExtendedFunctions
    /* Other case examples */
    genShortMsgSponge(1344, 256, 4096, "ShortMsgKAT_r1344c256.txt");
    /* Duplexing */
    //genDuplexKAT(1024, 576, "DuplexKAT_r1024c576.txt");
    //genDuplexKAT(1025, 575, "DuplexKAT_r1025c575.txt");
    genDuplexKAT(1026, 574, "DuplexKAT_r1026c574.txt");
    genDuplexKAT(1027, 573, "DuplexKAT_r1027c573.txt");
    //genDuplexKAT(1028, 572, "DuplexKAT_r1028c572.txt");
    //genDuplexKAT(1029, 571, "DuplexKAT_r1029c571.txt");
    //genDuplexKAT(1030, 570, "DuplexKAT_r1030c570.txt");
    //genDuplexKAT(1031, 569, "DuplexKAT_r1031c569.txt");
    //genDuplexKAT(1032, 568, "DuplexKAT_r1032c568.txt");
#endif

    return KAT_SUCCESS;
}

STATUS_CODES
genShortMsg(int hashbitlen)
{
    char        fn[32], line[SUBMITTER_INFO_LEN];
    int         msglen, msgbytelen, done, rv;
    BitSequence Msg[256], MD[64];
    FILE        *fp_in, *fp_out;
    
    if ( (fp_in = fopen("ShortMsgKAT.txt", "r")) == NULL ) {
        printf("Couldn't open <ShortMsgKAT.txt> for read\n");
        return KAT_FILE_OPEN_ERROR;
    }
    
    sprintf(fn, "ShortMsgKAT_%d.txt", hashbitlen);
    if ( (fp_out = fopen(fn, "w")) == NULL ) {
        printf("Couldn't open <%s> for write\n", fn);
        fclose(fp_in);
        return KAT_FILE_OPEN_ERROR;
    }
    fprintf(fp_out, "# %s\n", fn);
    if ( FindMarker(fp_in, "# Algorithm Name:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Algorithm Name:%s\n", line);
    }
    else {
        printf("genShortMsg: Couldn't read Algorithm Name\n");
        fclose(fp_in);
        fclose(fp_out);
        return KAT_HEADER_ERROR;
    }
    if ( FindMarker(fp_in, "# Principal Submitter:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Principal Submitter:%s\n", line);
    }
    else {
        printf("genShortMsg: Couldn't read Principal Submitter\n");
        return KAT_HEADER_ERROR;
    }
    
    done = 0;
    do {
        if ( FindMarker(fp_in, "Len = ") )
            rv = fscanf(fp_in, "%d", &msglen);
        else {
            done = 1;
            break;
        }
        msgbytelen = (msglen+7)/8;

        if ( !ReadHex(fp_in, Msg, msgbytelen, "Msg = ") ) {
            printf("ERROR: unable to read 'Msg' from <ShortMsgKAT.txt>\n");
            return KAT_DATA_ERROR;
        }
        Keccak_Hash(hashbitlen, Msg, msglen, MD);
        fprintf(fp_out, "\nLen = %d\n", msglen);
        fprintBstr(fp_out, "Msg = ", Msg, msgbytelen);
        fprintBstr(fp_out, "MD = ", MD, hashbitlen/8);
    } while ( !done );
    printf("finished ShortMsgKAT for <%d>\n", hashbitlen);
    
    fclose(fp_in);
    fclose(fp_out);
    
    return KAT_SUCCESS;
}

#ifdef AllowExtendedFunctions
STATUS_CODES
genShortMsgSponge(unsigned int rate, unsigned int capacity, int outputLength, const char *fileName)
{
    char        line[SUBMITTER_INFO_LEN];
    int         msglen, msgbytelen, done, rv;
    BitSequence Msg[256];
    BitSequence Squeezed[SqueezingOutputLength/8];
    spongeState   state;
    FILE        *fp_in, *fp_out;
    
    if (outputLength > SqueezingOutputLength) {
        printf("Requested output length too long.\n");
        return KAT_HASH_ERROR;
    }

    if ( (fp_in = fopen("ShortMsgKAT.txt", "r")) == NULL ) {
        printf("Couldn't open <ShortMsgKAT.txt> for read\n");
        return KAT_FILE_OPEN_ERROR;
    }
    
    if ( (fp_out = fopen(fileName, "w")) == NULL ) {
        printf("Couldn't open <%s> for write\n", fileName);
        fclose(fp_in);
        return KAT_FILE_OPEN_ERROR;
    }
    fprintf(fp_out, "# %s\n", fileName);
    if ( FindMarker(fp_in, "# Algorithm Name:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Algorithm Name:%s\n", line);
    }
    else {
        printf("genShortMsg: Couldn't read Algorithm Name\n");
        fclose(fp_in);
        fclose(fp_out);
        return KAT_HEADER_ERROR;
    }
    if ( FindMarker(fp_in, "# Principal Submitter:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Principal Submitter:%s\n", line);
    }
    else {
        printf("genShortMsg: Couldn't read Principal Submitter\n");
        return KAT_HEADER_ERROR;
    }
    
    done = 0;
    do {
        if ( FindMarker(fp_in, "Len = ") )
            rv = fscanf(fp_in, "%d", &msglen);
        else {
            done = 1;
            break;
        }
        msgbytelen = (msglen+7)/8;

        if ( !ReadHex(fp_in, Msg, msgbytelen, "Msg = ") ) {
            printf("ERROR: unable to read 'Msg' from <ShortMsgKAT.txt>\n");
            return KAT_DATA_ERROR;
        }
        fprintf(fp_out, "\nLen = %d\n", msglen);
        fprintBstr(fp_out, "Msg = ", Msg, msgbytelen);
        InitSponge(&state, rate, capacity);
        if ((msglen % 8 ) != 0)
            // From NIST convention to internal convention for last byte
            Msg[msgbytelen - 1] >>= 8 - (msglen % 8);
        Absorb(&state, Msg, msglen);
        Squeeze(&state, Squeezed, outputLength);
        fprintBstr(fp_out, "Squeezed = ", Squeezed, SqueezingOutputLength/8);
    } while ( !done );
    printf("finished ShortMsgKAT for <%s>\n", fileName);
    
    fclose(fp_in);
    fclose(fp_out);
    
    return KAT_SUCCESS;
}
#endif

STATUS_CODES
genLongMsg(int hashbitlen)
{
    char        fn[32], line[SUBMITTER_INFO_LEN];
    int         msglen, msgbytelen, done, rv;
    BitSequence Msg[4288], MD[64];
#ifdef AllowExtendedFunctions
    BitSequence Squeezed[SqueezingOutputLength/8];
    hashState   state;
#endif
    FILE        *fp_in, *fp_out;
    
    if ( (fp_in = fopen("LongMsgKAT.txt", "r")) == NULL ) {
        printf("Couldn't open <LongMsgKAT.txt> for read\n");
        return KAT_FILE_OPEN_ERROR;
    }
    
    sprintf(fn, "LongMsgKAT_%d.txt", hashbitlen);
    if ( (fp_out = fopen(fn, "w")) == NULL ) {
        printf("Couldn't open <%s> for write\n", fn);
        fclose(fp_in);
        return KAT_FILE_OPEN_ERROR;
    }
    fprintf(fp_out, "# %s\n", fn);
    if ( FindMarker(fp_in, "# Algorithm Name:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Algorithm Name:%s\n", line);
    }
    else {
        printf("genLongMsg: Couldn't read Algorithm Name\n");
        fclose(fp_in);
        fclose(fp_out);
        return KAT_HEADER_ERROR;
    }
    if ( FindMarker(fp_in, "# Principal Submitter:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Principal Submitter:%s\n\n", line);
    }
    else {
        printf("genLongMsg: Couldn't read Principal Submitter\n");
        return KAT_HEADER_ERROR;
    }
    
    done = 0;
    do {
        if ( FindMarker(fp_in, "Len = ") )
            rv = fscanf(fp_in, "%d", &msglen);
        else
            break;
        msgbytelen = (msglen+7)/8;

        if ( !ReadHex(fp_in, Msg, msgbytelen, "Msg = ") ) {
            printf("ERROR: unable to read 'Msg' from <LongMsgKAT.txt>\n");
            return KAT_DATA_ERROR;
        }
#ifdef AllowExtendedFunctions
        if (hashbitlen > 0)
            Keccak_Hash(hashbitlen, Msg, msglen, MD);
        else {
            Keccak_Init(&state, hashbitlen);
            Keccak_Update(&state, Msg, msglen);
            Keccak_Final(&state, 0);
            Squeeze(&state, Squeezed, SqueezingOutputLength);
        }
#else
        Keccak_Hash(hashbitlen, Msg, msglen, MD);
#endif
        fprintf(fp_out, "Len = %d\n", msglen);
        fprintBstr(fp_out, "Msg = ", Msg, msgbytelen);
#ifdef AllowExtendedFunctions
        if (hashbitlen > 0)
            fprintBstr(fp_out, "MD = ", MD, hashbitlen/8);
        else
            fprintBstr(fp_out, "Squeezed = ", Squeezed, SqueezingOutputLength/8);
#else
        fprintBstr(fp_out, "MD = ", MD, hashbitlen/8);
#endif
    } while ( !done );
    printf("finished LongMsgKAT for <%d>\n", hashbitlen);
    
    fclose(fp_in);
    fclose(fp_out);
    
    return KAT_SUCCESS;
}

STATUS_CODES
genExtremelyLongMsg(int hashbitlen)
{
    char        fn[32], line[SUBMITTER_INFO_LEN];
    BitSequence Text[65], MD[64];
#ifdef AllowExtendedFunctions
    BitSequence Squeezed[SqueezingOutputLength/8];
#endif
    int         i, repeat, rv;
    FILE        *fp_in, *fp_out;
    hashState   state;
    HashReturn  retval;
    
    if ( (fp_in = fopen("ExtremelyLongMsgKAT.txt", "r")) == NULL ) {
        printf("Couldn't open <ExtremelyLongMsgKAT.txt> for read\n");
        return KAT_FILE_OPEN_ERROR;
    }
    
    sprintf(fn, "ExtremelyLongMsgKAT_%d.txt", hashbitlen);
    if ( (fp_out = fopen(fn, "w")) == NULL ) {
        printf("Couldn't open <%s> for write\n", fn);
        fclose(fp_in);
        return KAT_FILE_OPEN_ERROR;
    }
    fprintf(fp_out, "# %s\n", fn);
    if ( FindMarker(fp_in, "# Algorithm Name:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Algorithm Name:%s\n", line);
    }
    else {
        printf("genExtremelyLongMsg: Couldn't read Algorithm Name\n");
        fclose(fp_in);
        fclose(fp_out);
        return KAT_HEADER_ERROR;
    }
    if ( FindMarker(fp_in, "# Principal Submitter:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Principal Submitter:%s\n\n", line);
    }
    else {
        printf("genExtremelyLongMsg: Couldn't read Principal Submitter\n");
        return KAT_HEADER_ERROR;
    }
    
    if ( FindMarker(fp_in, "Repeat = ") )
        rv = fscanf(fp_in, "%d", &repeat);
    else {
        printf("ERROR: unable to read 'Repeat' from <ExtremelyLongMsgKAT.txt>\n");
        return KAT_DATA_ERROR;
    }
    
    if ( FindMarker(fp_in, "Text = ") )
        rv = fscanf(fp_in, "%s", Text);
    else {
        printf("ERROR: unable to read 'Text' from <ExtremelyLongMsgKAT.txt>\n");
        return KAT_DATA_ERROR;
    }
    
//  memcpy(Text, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno", 64);
    
    if ( (retval = Keccak_Init(&state, hashbitlen)) != KAT_SUCCESS ) {
        printf("Keccak_Init returned <%d> in genExtremelyLongMsg\n", retval);
        return KAT_HASH_ERROR;
    }
    for ( i=0; i<repeat; i++ )
        if ( (retval = Keccak_Update(&state, Text, 512)) != KAT_SUCCESS ) {
            printf("Keccak_Update returned <%d> in genExtremelyLongMsg\n", retval);
            return KAT_HASH_ERROR;
        }
    if ( (retval = Keccak_Final(&state, MD)) != KAT_SUCCESS ) {
        printf("Keccak_Final returned <%d> in genExtremelyLongMsg\n", retval);
        return KAT_HASH_ERROR;
    }
#ifdef AllowExtendedFunctions
    if (hashbitlen == 0)
        Squeeze(&state, Squeezed, SqueezingOutputLength);
#endif
    fprintf(fp_out, "Repeat = %d\n", repeat);
    fprintf(fp_out, "Text = %s\n", Text);
#ifdef AllowExtendedFunctions
    if (hashbitlen > 0)
        fprintBstr(fp_out, "MD = ", MD, hashbitlen/8);
    else
        fprintBstr(fp_out, "Squeezed = ", Squeezed, SqueezingOutputLength/8);
#else
    fprintBstr(fp_out, "MD = ", MD, hashbitlen/8);
#endif
    printf("finished ExtremelyLongMsgKAT for <%d>\n", hashbitlen);
    
    fclose(fp_in);
    fclose(fp_out);
    
    return KAT_SUCCESS;
}

STATUS_CODES
genMonteCarlo(int hashbitlen)
{
    char        fn[32], line[SUBMITTER_INFO_LEN];
    BitSequence Seed[128], Msg[128], MD[64], Temp[128];
    int         i, j, bytelen, rv;
    FILE        *fp_in, *fp_out;
    
    if ( (fp_in = fopen("MonteCarlo.txt", "r")) == NULL ) {
        printf("Couldn't open <MonteCarlo.txt> for read\n");
        return KAT_FILE_OPEN_ERROR;
    }
    
    sprintf(fn, "MonteCarlo_%d.txt", hashbitlen);
    if ( (fp_out = fopen(fn, "w")) == NULL ) {
        printf("Couldn't open <%s> for write\n", fn);
        fclose(fp_in);
        return KAT_FILE_OPEN_ERROR;
    }
    fprintf(fp_out, "# %s\n", fn);
    if ( FindMarker(fp_in, "# Algorithm Name:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Algorithm Name:%s\n", line);
    }
    else {
        printf("genMonteCarlo: Couldn't read Algorithm Name\n");
        fclose(fp_in);
        fclose(fp_out);
        return KAT_HEADER_ERROR;
    }
    if ( FindMarker(fp_in, "# Principal Submitter:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Principal Submitter:%s\n\n", line);
    }
    else {
        printf("genMonteCarlo: Couldn't read Principal Submitter\n");
        return KAT_HEADER_ERROR;
    }
    
    if ( !ReadHex(fp_in, Seed, 128, "Seed = ") ) {
        printf("ERROR: unable to read 'Seed' from <MonteCarlo.txt>\n");
        return KAT_DATA_ERROR;
    }
    
    bytelen = hashbitlen / 8;
    memcpy(Msg, Seed, 128);
    fprintBstr(fp_out, "Seed = ", Seed, 128);
    for ( j=0; j<100; j++ ) {
        for ( i=0; i<1000; i++ ) {
            Keccak_Hash(hashbitlen, Msg, 1024, MD);
            memcpy(Temp, Msg, 128-bytelen);
            memcpy(Msg, MD, bytelen);
            memcpy(Msg+bytelen, Temp, 128-bytelen);
        }
        fprintf(fp_out, "\nj = %d\n", j);
        fprintBstr(fp_out, "MD = ", MD, bytelen);
    }
    printf("finished MonteCarloKAT for <%d>\n", hashbitlen);

    fclose(fp_in);
    fclose(fp_out);
    
    return KAT_SUCCESS;
}

#ifdef AllowExtendedFunctions
STATUS_CODES
genMonteCarloSqueezing(int hashbitlen)
{
    char        fn[32], line[SUBMITTER_INFO_LEN];
    BitSequence Seed[128], MD[64];
    int         i, j, bytelen, rv;
    FILE        *fp_in, *fp_out;
    hashState   state;
    HashReturn  retval;
    
    if ( (fp_in = fopen("MonteCarlo.txt", "r")) == NULL ) {
        printf("Couldn't open <MonteCarlo.txt> for read\n");
        return KAT_FILE_OPEN_ERROR;
    }
    
    sprintf(fn, "MonteCarlo_%d.txt", hashbitlen);
    if ( (fp_out = fopen(fn, "w")) == NULL ) {
        printf("Couldn't open <%s> for write\n", fn);
        fclose(fp_in);
        return KAT_FILE_OPEN_ERROR;
    }
    fprintf(fp_out, "# %s\n", fn);
    if ( FindMarker(fp_in, "# Algorithm Name:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Algorithm Name:%s\n", line);
    }
    else {
        printf("genMonteCarlo: Couldn't read Algorithm Name\n");
        fclose(fp_in);
        fclose(fp_out);
        return KAT_HEADER_ERROR;
    }
    if ( FindMarker(fp_in, "# Principal Submitter:") ) {
        rv = fscanf(fp_in, "%[^\n]\n", line);
        fprintf(fp_out, "# Principal Submitter:%s\n\n", line);
    }
    else {
        printf("genMonteCarlo: Couldn't read Principal Submitter\n");
        return KAT_HEADER_ERROR;
    }
    
    if ( !ReadHex(fp_in, Seed, 128, "Seed = ") ) {
        printf("ERROR: unable to read 'Seed' from <MonteCarlo.txt>\n");
        return KAT_DATA_ERROR;
    }
    
    fprintBstr(fp_out, "Seed = ", Seed, 128);

    if ( (retval = Keccak_Init(&state, hashbitlen)) != KAT_SUCCESS ) {
        printf("Keccak_Init returned <%d> in genMonteCarloSqueezing\n", retval);
        return KAT_HASH_ERROR;
    }
    if ( (retval = Keccak_Update(&state, Seed, 128*8)) != KAT_SUCCESS ) {
        printf("Keccak_Update returned <%d> in genMonteCarloSqueezing\n", retval);
        return KAT_HASH_ERROR;
    }
    if ( (retval = Keccak_Final(&state, 0)) != KAT_SUCCESS ) {
        printf("Keccak_Final returned <%d> in genMonteCarloSqueezing\n", retval);
        return KAT_HASH_ERROR;
    }
    bytelen = 64;
    for ( j=0; j<100; j++ ) {
        for ( i=0; i<1000; i++ ) {
            if ( (retval = (HashReturn)Squeeze(&state, MD, bytelen*8)) != KAT_SUCCESS ) {
                printf("Squeeze returned <%d> in genMonteCarloSqueezing\n", retval);
                return KAT_HASH_ERROR;
            }
        }
        fprintf(fp_out, "\nj = %d\n", j);
        fprintBstr(fp_out, "MD = ", MD, bytelen);
    }
    printf("finished MonteCarloKAT for <%d>\n", hashbitlen);

    fclose(fp_in);
    fclose(fp_out);
    
    return KAT_SUCCESS;
}

STATUS_CODES
genDuplexKAT(unsigned int rate, unsigned int capacity, const char *fileName)
{
    int inLen, inByteLen, outLen, outByteLen, done, rv;
    BitSequence in[256];
    BitSequence out[256];
    FILE *fp_in, *fp_out;
    duplexState   state;
    
    if ( (fp_in = fopen("DuplexKAT.txt", "r")) == NULL ) {
        printf("Couldn't open <DuplexKAT.txt> for read\n");
        return KAT_FILE_OPEN_ERROR;
    }
    
    if ( (fp_out = fopen(fileName, "w")) == NULL ) {
        printf("Couldn't open <%s> for write\n", fileName);
        fclose(fp_in);
        return KAT_FILE_OPEN_ERROR;
    }
    fprintf(fp_out, "# %s\n", fileName);
    fprintf(fp_out, "# Algorithm: Duplex[f=Keccak-f[1600], pad=pad10*1, r=%d, c=%d, \xCF\x81max=%d]\n", rate, capacity, rate-2);
  
    InitDuplex(&state, rate, capacity);
    done = 0;
    outLen = rate;
    outByteLen = (outLen+7)/8;
    do {
        if ( FindMarker(fp_in, "InLen = ") )
            rv = fscanf(fp_in, "%d", &inLen);
        else {
            done = 1;
            break;
        }
        inByteLen = (inLen+7)/8;

        if ( !ReadHex(fp_in, in, inByteLen, "In = ") ) {
            printf("ERROR: unable to read 'In' from <DuplexKAT.txt>\n");
            return KAT_DATA_ERROR;
        }
        if (inLen <= rate-2) {
            fprintf(fp_out, "\nInLen = %d\n", inLen);
            fprintBstr(fp_out, "In = ", in, inByteLen);
            Duplexing(&state, in, inLen, out, outLen);
            fprintf(fp_out, "OutLen = %d\n", outLen);
            fprintBstr(fp_out, "Out = ", out, outByteLen);
        }
    } while ( !done );
    printf("finished DuplexKAT for <%s>\n", fileName);
    
    fclose(fp_in);
    fclose(fp_out);
    
    return KAT_SUCCESS;
}
#endif

//
// ALLOW TO READ HEXADECIMAL ENTRY (KEYS, DATA, TEXT, etc.)
//
int
FindMarker(FILE *infile, const char *marker)
{
    char    line[MAX_MARKER_LEN];
    int     i, len;

    len = (int)strlen(marker);
    if ( len > MAX_MARKER_LEN-1 )
        len = MAX_MARKER_LEN-1;

    for ( i=0; i<len; i++ )
        if ( (line[i] = fgetc(infile)) == EOF )
            return 0;
    line[len] = '\0';

    while ( 1 ) {
        if ( !strncmp(line, marker, len) )
            return 1;

        for ( i=0; i<len-1; i++ )
            line[i] = line[i+1];
        if ( (line[len-1] = fgetc(infile)) == EOF )
            return 0;
        line[len] = '\0';
    }

    // shouldn't get here
    return 0;
}

//
// ALLOW TO READ HEXADECIMAL ENTRY (KEYS, DATA, TEXT, etc.)
//
int
ReadHex(FILE *infile, BitSequence *A, int Length, const char *str)
{
    int         i, ch, started;
    BitSequence ich;

    if ( Length == 0 ) {
        A[0] = 0x00;
        return 1;
    }
    memset(A, 0x00, Length);
    started = 0;
    if ( FindMarker(infile, str) )
        while ( (ch = fgetc(infile)) != EOF ) {
            if ( !isxdigit(ch) ) {
                if ( !started ) {
                    if ( ch == '\n' )
                        break;
                    else
                        continue;
                }
                else
                    break;
            }
            started = 1;
            if ( (ch >= '0') && (ch <= '9') )
                ich = ch - '0';
            else if ( (ch >= 'A') && (ch <= 'F') )
                ich = ch - 'A' + 10;
            else if ( (ch >= 'a') && (ch <= 'f') )
                ich = ch - 'a' + 10;
	    else
		return 1;
            
            for ( i=0; i<Length-1; i++ )
                A[i] = (A[i] << 4) | (A[i+1] >> 4);
            A[Length-1] = (A[Length-1] << 4) | ich;
        }
    else
        return 0;

    return 1;
}

void
fprintBstr(FILE *fp, const char *S, BitSequence *A, int L)
{
    int     i;

    fprintf(fp, "%s", S);

    for ( i=0; i<L; i++ )
        fprintf(fp, "%02X", A[i]);

    if ( L == 0 )
        fprintf(fp, "00");

    fprintf(fp, "\n");
}
