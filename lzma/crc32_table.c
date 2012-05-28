///////////////////////////////////////////////////////////////////////////////
//
/// \file       crc32_table.c
/// \brief      Precalculated CRC32 table with correct endianness
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

/*
 * This gives us BYTE_ORDER macro both on Linux and Solaris derived
 * systems.
 */
#include <arpa/nameser_compat.h>

#if BYTE_ORDER == LITTLE_ENDIAN
#       include "crc32_table_le.h"
#else
#       include "crc32_table_be.h"
#endif
