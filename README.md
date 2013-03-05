Pcompress
=========

Copyright (C) 2012 Moinak Ghosh. All rights reserved.
Use is subject to license terms.
moinakg (_at) gma1l _dot com.
Comments, suggestions, code, rants etc are welcome.

Pcompress is a utility to do compression and decompression in parallel by
splitting input data into chunks. It has a modular structure and includes
support for multiple algorithms like LZMA, Bzip2, PPMD, etc, with SKEIN/
SHA checksums for data integrity. It can also do Lempel-Ziv pre-compression
(derived from libbsc) to improve compression ratios across the board. SSE
optimizations for the bundled LZMA are included. It also implements
chunk-level Content-Aware Deduplication and Delta Compression features
based on a Semi-Rabin Fingerprinting scheme. Delta Compression is done
via the widely popular bsdiff algorithm. Similarity is detected using a
technique based on MinHashing. When doing chunk-level dedupe it attempts
to merge adjacent non-duplicate blocks index entries into a single larger
entry to reduce metadata. In addition to all these it can internally split
chunks at rabin boundaries to help dedupe and compression.

It has low metadata overhead and overlaps I/O and compression to achieve
maximum parallelism. It also bundles a simple slab allocator to speed
repeated allocation of similar chunks. It can work in pipe mode, reading
from stdin and writing to stdout. It also provides adaptive compression
modes in which data analysis heuristics are used to identify near-optimal
algorithms per chunk. Finally it supports 14 compression levels to allow
for ultra compression modes in some algorithms.

Pcompress also supports encryption via AES and uses Scrypt from Tarsnap
for Password Based Key generation. A unique key is generated per session
even if the same password is used and HMAC is used to do authentication.

NOTE: This utility is Not an archiver. It compresses only single files or
      datastreams. To archive use something else like tar, cpio or pax.

Usage
=====

    To compress a file:
       pcompress -c <algorithm> [-l <compress level>] [-s <chunk size>] <file>
       Where <algorithm> can be the folowing:
       lzfx   - Very fast and small algorithm based on LZF.
       lz4    - Ultra fast, high-throughput algorithm reaching RAM B/W at level1.
       zlib   - The base Zlib format compression (not Gzip).
       lzma   - The LZMA (Lempel-Ziv Markov) algorithm from 7Zip.
       lzmaMt - Multithreaded version of LZMA. This is a faster version but
                uses more memory for the dictionary. Thread count is balanced
                between chunk processing threads and algorithm threads.
       bzip2  - Bzip2 Algorithm from libbzip2.
       ppmd   - The PPMd algorithm excellent for textual data. PPMd requires
                at least 64MB X CPUs more memory than the other modes.

       libbsc - A Block Sorting Compressor using the Burrows Wheeler Transform
                like Bzip2 but runs faster and gives better compression than
                Bzip2 (See: libbsc.com).

       adapt  - Adaptive mode where ppmd or bzip2 will be used per chunk,
                depending on heuristics. If at least 50% of the input data is
                7-bit text then PPMd will be used otherwise Bzip2.
       adapt2 - Adaptive mode which includes ppmd and lzma. If at least 80% of
                the input data is 7-bit text then PPMd will be used otherwise
                LZMA. It has significantly more memory usage than adapt.
       none   - No compression. This is only meaningful with -D and -E so Dedupe
                can be done for post-processing with an external utility.
       <chunk_size> - This can be in bytes or can use the following suffixes:
                g - Gigabyte, m - Megabyte, k - Kilobyte.
                Larger chunks produce better compression at the cost of memory.
       <compress_level> - Can be a number from 0 meaning minimum and 14 meaning
                maximum compression.

NOTE: The option "libbsc" uses  Ilya Grebnov's block sorting compression library
      from http://libbsc.com/ . It is only available if pcompress in built with
      that library. See INSTALL file for details.
      
    To decompress a file compressed using above command:
       pcompress -d <compressed file> <target file>

    To operate as a pipe, read from stdin and write to stdout:
       pcompress -p ...

    Attempt Rabin fingerprinting based deduplication on chunks:
       pcompress -D ...
       pcompress -D -r ... - Do NOT split chunks at a rabin boundary. Default
                             is to split.

    Perform Delta Encoding in addition to Identical Dedup:
       pcompress -E ... - This also implies '-D'. This performs Delta Compression
                          between 2 blocks if they are 40% to 60% similar. The
                          similarity %age is selected based on the dedupe block
                          size to balance performance and effectiveness.
       pcompress -EE .. - This causes Delta Compression to happen if 2 blocks are
                          at least 40% similar regardless of block size. This can
                          effect greater final compression ratio at the cost of
                          higher processing overhead.

    Number of threads can optionally be specified: -t <1 - 256 count>
    Other flags:
       '-L' -     Enable LZP pre-compression. This improves compression ratio of all
                  algorithms with some extra CPU and very low RAM overhead. Using
                  delta encoding in conjunction with this may not always be beneficial.
                  However Adaptive Delta Encoding is beneficial along with this.

       '-P' -     Enable Adaptive Delta Encoding. It can improve compresion ratio further
                  for data containing tables of numerical values especially if those are
                  in an arithmetic series. In this implementation basic Delta Encoding is
                  combined with Run-Length encoding and Matrix transpose
       NOTE -     Both -L and -P can be used together to give maximum benefit on most
                  datasets.

       '-S' <cksum>
            -     Specify chunk checksum to use: CRC64, SKEIN256, SKEIN512, SHA256 and
                  SHA512. Default one is SKEIN256. The implementation actually uses SKEIN
                  512-256. This is 25% slower than simple CRC64 but is many times more
                  robust than CRC64 in detecting data integrity errors. SKEIN is a
                  finalist in the NIST SHA-3 standard selection process and is one of
                  the fastest in the group, especially on x86 platforms. BLAKE is faster
                  than SKEIN on a few platforms.
                  SKEIN 512-256 is about 60% faster than SHA 512-256 on x64 platforms.

       '-F' -     Perform Fixed Block Deduplication. This is faster than fingerprinting
                  based content-aware deduplication in some cases. However this is mostly
                  usable for disk dumps especially virtual machine images. This generally
                  gives lower dedupe ratio than content-aware dedupe (-D) and does not
                  support delta compression.

       '-B' <1..5>
            -     Specify an average Dedupe block size. 1 - 4K, 2 - 8K ... 5 - 64K.
       '-M' -     Display memory allocator statistics
       '-C' -     Display compression statistics

    Encryption flags:
       '-e <ALGO>'
                  Encrypt chunks using the given encryption algorithm. The algo parameter
                  can be one of AES or SALSA20. Both are used in CTR stream encryption
                  mode.
                  The password can be prompted from the user or read from a file. Unique
                  keys are generated every time pcompress is run even when giving the same
                  password. Of course enough info is stored in the compresse file so that
                  the key used for the file can be re-created given the correct password.

                  Default key length if 256 bits but can be reduced to 128 bits using the
                  '-k' option.

                  The Scrypt algorithm from Tarsnap is used
                  (See: http://www.tarsnap.com/scrypt.html) for generating keys from
                  passwords. The CTR mode AES mechanism from Tarsnap is also utilized.

       '-w <pathname>'
                  Provide a file which contains the encryption password. This file must
                  be readable and writable since it is zeroed out after the password is
                  read.

       '-k <key length>'
                  Specify the key length. Can be 16 for 128 bit keys or 32 for 256 bit
                  keys. Default value is 32 for 256 bit keys.

NOTE: When using pipe-mode via -p the only way to provide a password is to use '-w'.

Environment Variables
=====================

Set ALLOCATOR_BYPASS=1 in the environment to avoid using the the built-in
allocator. Due to the the way it rounds up an allocation request to the nearest
slab the built-in allocator can allocate extra unused memory. In addition you
may want to use a different allocator in your environment.

Examples
========

Compress "file.tar" using bzip2 level 6, 64MB chunk size and use 4 threads. In
addition perform identity deduplication and delta compression prior to compression.

    pcompress -D -E -c bzip2 -l6 -s64m -t4 file.tar

Compress "file.tar" using extreme compression mode of LZMA and a chunk size of
of 1GB. Allow pcompress to detect the number of CPU cores and use as many threads.

    pcompress -c lzma -l14 -s1g file.tar

Compress "file.tar" using lz4 at max compression with LZ-Prediction pre-processing
and encryption enabled. Chunksize is 100M:

    pcompress -c lz4 -l3 -e -L -s100m file.tar

Compression Algorithms
======================

LZFX	- Ultra Fast, average compression. This algorithm is the fastest overall.
	  Levels: 1 - 5
LZ4	- Very Fast, better compression than LZFX.
	  Levels: 1 - 3
Zlib	- Fast, better compression.
	  Levels: 1 - 9
Bzip2	- Slow, much better compression than Zlib.
	  Levels: 1 - 9

LZMA	- Very slow. Extreme compression.
	  Levels: 1 - 14
          Till level 9 it is standard LZMA parameters. Levels 10 - 12 use
          more memory and higher match iterations so are slower. Levels
          13 and 14 use larger dictionaries upto 256MB and really suck up
          RAM. Use these levels only if you have at the minimum 4GB RAM on
          your system.

PPMD	- Slow. Extreme compression for Text, average compression for binary.
          In addition PPMD decompression time is also high for large chunks.
          This requires lots of RAM similar to LZMA.
	  Levels: 1 - 14.

Adapt	- Synthetic mode with text/binary detection. For pure text data PPMD is
          used otherwise Bzip2 is selected per chunk.
	  Levels: 1 - 14
Adapt2	- Slower synthetic mode. For pure text data PPMD is otherwise LZMA is
          applied. Can give very good compression ratio when splitting file
          into multiple chunks.
	  Levels: 1 - 14
          Since both LZMA and PPMD are used together memory requirements are
          large especially if you are also using extreme levels above 10. For
          example with 100MB chunks, Level 14, 2 threads and with or without
          dedupe, it uses upto 2.5GB physical RAM (RSS).

It is possible for a single chunk to span the entire file if enough RAM is
available. However for adaptive modes to be effective for large files, especially
multi-file archives splitting into chunks is required so that best compression
algorithm can be selected for textual and binary portions.

Pre-Processing Algorithms
=========================
As can be seen above a multitude of pre-processing algorithms are available that
provide further compression effectiveness beyond what the usual compression
algorithms can achieve by themselves. These are summarized below:

1) Deduplication     : Per-Chunk (or per-segment) deduplication based on Rabin
                       fingerprinting.

2) Delta Compression : A similarity based (minhash) comparison of Rabin blocks. Two
                       blocks at least 60% similar with each other are diffed using
                       bsdiff.

3) LZP               : LZ Prediction is a variant of LZ77 that replaces repeating
                       runs of text with shorter codes.

4) Adaptive Delta    : This is a simple form of Delta Encoding where arithmetic
                       progressions are detected in the data stream and collapsed
                       via Run-Length encoding.

4) Matrix Transpose  : This is used automatically in Delta Encoding and Deduplication.
                       This attempts to transpose columnar repeating sequences of
                       bytes into row-wise sequences so that compression algorithms
                       can work better.

Memory Usage
============
As can be seen from above memory usage can vary greatly based on compression/
pre-processing algorithms and chunk size. A variety of configurations are possible
depending on resource availability in the system.

The minimum possible meaningful settings while still giving about 50% compression
ratio and very high speed is with the LZFX algorithm with 1MB chunk size and 2
threads:

        pcompress -c lzfx -l2 -s1m -t2 <file>

This uses about 6MB of physical RAM (RSS). Earlier versions of the utility before
the 0.9 release comsumed much more memory. This was improved in the later versions.
When using Linux the virtual memory consumption may appear to be very high but it
is just address space usage rather than actual RAM and should be ignored. It is only
the RSS that matters. This is a result of the memory arena mechanism in Glibc that
improves malloc() performance for multi-threaded applications.


