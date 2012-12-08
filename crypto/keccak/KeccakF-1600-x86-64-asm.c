/*
The Keccak sponge function, designed by Guido Bertoni, Joan Daemen,
Michaël Peeters and Gilles Van Assche. For more information, feedback or
questions, please refer to our website: http://keccak.noekeon.org/

Implementation by Ronny Van Keer,
hereby denoted as "the implementer".

To the extent possible under law, the implementer has waived all copyright
and related or neighboring rights to the source code in this file.
http://creativecommons.org/publicdomain/zero/1.0/
*/

#include <string.h>
#include "KeccakF-1600-interface.h"

#define	UseBebigokimisa

typedef unsigned char UINT8;
typedef unsigned long long int UINT64;

void KeccakInitialize()
{
}

void KeccakExtract(const unsigned char *state, unsigned char *data, unsigned int laneCount)
{
    memcpy(data, state, laneCount*8);
#ifdef UseBebigokimisa
    if (laneCount > 8) 
    {
        ((UINT64*)data)[ 1] = ~((UINT64*)data)[ 1];
        ((UINT64*)data)[ 2] = ~((UINT64*)data)[ 2];
        ((UINT64*)data)[ 8] = ~((UINT64*)data)[ 8];

        if (laneCount > 12) 
        {
            ((UINT64*)data)[12] = ~((UINT64*)data)[12];
            if (laneCount > 17) 
            {
                ((UINT64*)data)[17] = ~((UINT64*)data)[17];
                if (laneCount > 20) 
                {
                    ((UINT64*)data)[20] = ~((UINT64*)data)[20];
                }
            }
        }
    }
    else
    {
		if (laneCount > 1) 
		{
			((UINT64*)data)[ 1] = ~((UINT64*)data)[ 1];
			if (laneCount > 2) 
			{
				((UINT64*)data)[ 2] = ~((UINT64*)data)[ 2];
            }
        }
    }

#endif
}
