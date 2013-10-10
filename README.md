Pcompress
=========

Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
Use is subject to license terms.
moinakg (_at) gma1l _dot com.
Comments, suggestions, code, rants etc are welcome.

Pcompress is a utility to do compression and decompression in parallel by
splitting input data into chunks. It has a modular structure and includes
support for multiple algorithms like LZMA, Bzip2, PPMD, etc, with SKEIN/
SHA checksums for data integrity. It can also do Lempel-Ziv pre-compression
(derived from libbsc) to improve compression ratios across the board. SSE
optimizations for the bundled LZMA are included. It also implements
Variable Block Deduplication and Delta Compression features based on a
Semi-Rabin Fingerprinting scheme. Delta Compression is done via the widely
popular bsdiff algorithm. Similarity is detected using a technique based
on MinHashing. When doing Dedupe it attempts to merge adjacent non-
duplicate block index entries into a single larger entry to reduce metadata.
In addition to all these it can internally split chunks at rabin boundaries
to help Dedupe and compression.

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

Links of Interest
=================

Project Home Page: http://moinakg.github.io/pcompress/

http://moinakg.github.io/pcompress/#deduplication-chunking-analysis

http://moinakg.github.io/pcompress/#compression-benchmarks

http://moinakg.wordpress.com/2013/04/26/pcompress-2-0-with-global-deduplication/

http://moinakg.wordpress.com/2013/03/26/coordinated-parallelism-using-semaphores/

http://moinakg.wordpress.com/2013/06/11/architecture-for-a-deduplicated-archival-store-part-1/

http://moinakg.wordpress.com/2013/06/15/architecture-for-a-deduplicated-archival-store-part-2/

Usage
=====

    To compress a file:
       pcompress -c <algorithm> [-l <compress level>] [-s <chunk size>] <file> [<target file>]

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
                at least 64MB X core-count more memory than the other modes.

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
                In case of Global Deduplication (see below) this chunk size is
                just a hint and may get automatically adjusted.
       <compress_level> - Can be a number from 0 meaning minimum and 14 meaning
                maximum compression.
       <target file>    - Optional argument specifying the destination compressed
                file. The '.pz' extension is appended. If this is '-' then
                compressed output goes to stdout. If this argument is omitted then
                source filename is used with the extension '.pz' appended.

NOTE: The option "libbsc" uses  Ilya Grebnov's block sorting compression library
      from http://libbsc.com/ . It is only available if pcompress in built with
      that library. See INSTALL file for details.
      
    To decompress a file compressed using above command:
       pcompress -d <compressed file> <target file>
       
    <compressed file> can be '-' to indicate reading from stdin while write goes
    to <target file>

    To operate as a full pipe, read from stdin and write to stdout:
       pcompress -p ...

    Attempt Rabin fingerprinting based deduplication on a per-chunk basis:
       pcompress -D ...

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
            -     Specify chunk checksum to use:

                     CRC64 - Extremely Fast 64-bit CRC from LZMA SDK.
                    SHA256 - SHA512/256 version of Intel's optimized (SSE,AVX) SHA2 for x86.
                    SHA512 - SHA512 version of Intel's optimized (SSE,AVX) SHA2 for x86.
                 KECCAK256 - Official 256-bit NIST SHA3 optimized implementation.
                 KECCAK512 - Official 512-bit NIST SHA3 optimized implementation.
                  BLAKE256 - Very fast 256-bit BLAKE2, derived from the NIST SHA3
                             runner-up BLAKE.
                  BLAKE512 - Very fast 256-bit BLAKE2, derived from the NIST SHA3
                             runner-up BLAKE.

       '-F' -     Perform Fixed Block Deduplication. This is faster than fingerprinting
                  based content-aware deduplication in some cases. However this is mostly
                  usable for disk dumps especially virtual machine images. This generally
                  gives lower dedupe ratio than content-aware dedupe (-D) and does not
                  support delta compression.

       '-B' <0..5>
            -     Specify an average Dedupe block size. 0 - 2K, 1 - 4K, 2 - 8K ... 5 - 64K.
                  Default deduplication block size is 4KB for Global Deduplication and 2KB
                  otherwise.
       '-B' 0
            -     This uses blocks as small as 2KB for deduplication. This option can be
                  used for datasets of a few GBs to a few hundred TBs in size depending on
                  available RAM.
                  
                  Caveats:
                  In some cases like LZMA with extreme compression levels and with '-L' and
                  '-P' preprocessing enabled, this can result in lower compression as compared
                  to using '-B 1'.
                  For fast compression algorithms like LZ4 and Zlib this should always benefit.
                  However please test on your sample data with your desired compression
                  algorithm to verify the results.

       '-M' -     Display memory allocator statistics
       '-C' -     Display compression statistics

    Global Deduplication:
       '-G' -     This flag enables Global Deduplication. This makes pcompress maintain an
                  in-memory index to lookup cryptographic block hashes for duplicates. Once
                  a duplicate is found it is replaced with a reference to the original block.
                  This allows detecting and eliminating duplicate blocks across the entire
                  dataset. In contrast using only '-D' or '-F' flags does deduplication only
                  within the chunk but uses less memory and is much faster than Global Dedupe.

                  The '-G' flag can be combined with either '-D' or '-F' flags to indicate
                  rabin chunking or fixed chunking respectively. If these flags are not
                  specified then the default is to assume rabin chunking via '-D'.
                  All other Dedupe flags have the same meanings in this context.

                  Delta Encoding is not supported with Global Deduplication at this time. The
                  in-memory hashtable index can use upto 75% of free RAM depending on the size
                  of the dataset. In Pipe mode the index will always use 75% of free RAM since
                  the dataset size is not known. This is the simple full block index mode. If
                  the available RAM is not enough to hold all block checksums then older block
                  entries are discarded automatically from the matching hash slots.

                  If pipe mode is not used and the given dataset is a file then Pcompress
                  checks whether the index size will exceed three times of 75% of the available
                  free RAM. In such a case it automatically switches to a Segmented Deduplication
                  mode. Here data is first split into blocks as above. Then upto 2048 blocks are
                  grouped together to form a larger segment. The individual block hashes for a
                  segment are stored on a tempfile on disk. A few min-values hashes are then
                  computed from the block hashes of the segment which are then loaded into the
                  index. These hashes are used to detect segments that are approximately similar
                  to each other. Once found the block hashes of the matching segments are loaded
                  from the temp file and actual deduplication is performed. This allows the
                  in-memory index size to be approximately 0.0025% of the total dataset size and
                  requires very few disk reads for every 2048 blocks processed.
                  
                  In pipe mode Global Deduplication always uses a segmented similarity based
                  index. It allows efficient network transfer of large data.

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

The variable PCOMPRESS_INDEX_MEM can be set to limit memory used by the Global
Deduplication Index. The number specified is in multiples of a megabyte.

The variable PCOMPRESS_CACHE_DIR can point to a directory where some temporary
files relating to the Global Deduplication process can be stored. This for example
can be a directory on a Solid State Drive to speed up Global Deduplication. The
space used in this directory is proportional to the size of the dataset being
processed and is slightly more than 8KB for every 1MB of data.

The default checksum used for block hashes during Global Deduplication is SHA256.
However this can be changed by setting the PCOMPRESS_CHUNK_HASH_GLOBAL environment
variable. The list of allowed checksums for this is:

SHA256   , SHA512
KECCAK256, KECCAK512
BLAKE256 , BLAKE512
SKEIN256 , SKEIN512

Even though SKEIN is not supported as a chunk checksum (not deemed necessary
because BLAKE2 is available) it can be used as a dedupe block checksum. One may
ask why? The reasoning is we depend on hashes to find duplicate blocks. Now SHA256
is the default because it is known to be robust and unbroken till date. Proven as
yet in the field. However one may want a faster alternative so we have choices
from the NIST SHA3 finalists in the form of SKEIN and BLAKE which are neck to
neck with SKEIN getting an edge. SKEIN and BLAKE have seen extensive cryptanalysis
in the intervening years and are unbroken with only marginal theoretical issues
determined. BLAKE2 is a derivative of BLAKE and is tremendously fast but has not
seen much specific cryptanalysis as yet, even though it is not new but just a
performance optimized derivate. So cryptanalysis that applies to BLAKE should
also apply and justify BLAKE2. However the paranoid may well trust SKEIN a bit
more than BLAKE2 and SKEIN while not being as fast as BLAKE2 is still a lot faster
than SHA2.

Examples
========

Simple compress "file.tar" using zlib(gzip) algorithm. Default chunk or per-thread
segment size is 8MB and default compression level is 6.

    pcompress -c zlib file.tar

Compress "file.tar" using bzip2 level 6, 64MB chunk size and use 4 threads. In
addition perform identity deduplication and delta compression prior to compression.

    pcompress -D -E -c bzip2 -l6 -s64m -t4 file.tar
    
Compress "file.tar" using zlib and also perform Global Deduplication. Default block
size used for deduplication is 4KB. Also redirect the compressed output to stdout and
send it to a compressed file at a different path.

    pcompress -G -c zlib -l9 -s10m file.tar - > /path/to/compress_file.tar.pz
    
Perform the same as above but this time use a deduplication block size of 8KB.

    pcompress -G -c zlib -l9 -B2 -s10m file.tar - > /path/to/compress_file.tar.pz

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


