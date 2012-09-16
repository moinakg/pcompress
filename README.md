Pcompress
=========

Copyright (C) 2012 Moinak Ghosh. All rights reserved.
Use is subject to license terms.

Pcompress is a utility to do compression and decompression in parallel by
splitting input data into chunks. It has a modular structure and includes
support for multiple algorithms like LZMA, Bzip2, PPMD, etc, with SKEIN
checksums for data integrity. It can also do Lempel-Ziv pre-compression
(derived from libbsc) to improve compression ratios across the board. SSE
optimizations for the bundled LZMA are included. It also implements
chunk-level Content-Aware Deduplication and Delta Compression features
based on a Semi-Rabin Fingerprinting scheme. Delta Compression is done
via the widely popular bsdiff algorithm. Similarity is detected using a
custom hashing of maximal features of a block. When doing chunk-level
dedupe it attempts to merge adjacent non-duplicate blocks index entries
into a single larger entry to reduce metadata. In addition to all these it
can internally split chunks at rabin boundaries to help dedupe and
compression.

It has low metadata overhead and overlaps I/O and compression to achieve
maximum parallelism. It also bundles a simple slab allocator to speed
repeated allocation of similar chunks. It can work in pipe mode, reading
from stdin and writing to stdout. It also provides some adaptive compression
modes in which multiple algorithms are tried per chunk to determine the best
one for the given chunk. Finally it supports 14 compression levels to allow
for ultra compression modes in some algorithms.

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
                depending on which one produces better compression. This mode
                is obviously fairly slow and requires lots of memory.
       adapt2 - Adaptive mode which includes ppmd and lzma. This requires
                more memory than adapt mode, is slower and potentially gives
                the best compression.
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

    Perform Delta Encoding in addition to Exact Dedup:
       pcompress -E ... - This also implies '-D'.

    Number of threads can optionally be specified: -t <1 - 256 count>
    Other flags:
       '-L' -     Enable LZP pre-compression. This improves compression ratio of all
                  algorithms with some extra CPU and very low RAM overhead. Using
                  delta encoding in conjunction with this may not always be beneficial.
       '-S' <cksum>
            -     Specify chunk checksum to use: CRC64, SKEIN256, SKEIN512
                  Default one is SKEIN256. The implementation actually uses SKEIN
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
       '-M' -     Display memory allocator statistics
       '-C' -     Display compression statistics

NOTE: It is recommended not to use '-L' with libbsc compression since libbsc uses
      LZP internally as well.

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

Adapt	- Very slow synthetic mode. Both Bzip2 and PPMD are tried per chunk and
	  better result selected.
	  Levels: 1 - 14
Adapt2	- Ultra slow synthetic mode. Both LZMA and PPMD are tried per chunk and
	  better result selected. Can give best compression ratio when splitting
	  file into multiple chunks.
	  Levels: 1 - 14
          Since both LZMA and PPMD are used together memory requirements are
          quite extensive especially if you are also using extreme levels above
          10. For example with 64MB chunk, Level 14, 2 threads and with or without
          dedupe, it uses upto 3.5GB physical RAM and requires 6GB of virtual
          memory space.

It is possible for a single chunk to span the entire file if enough RAM is
available. However for adaptive modes to be effective for large files, especially
multi-file archives splitting into chunks is required so that best compression
algorithm can be selected for textual and binary portions.

Caveats
=======
This utility is not meant for resource constrained environments. Minimum memory
usage (RES/RSS) with barely meaningful settings is around 10MB. This occurs when
using the minimal LZFX compression algorithm at level 2 with a 1MB chunk size and
running 2 threads.
Normally this utility requires lots of RAM depending on compression algorithm,
compression level, and dedupe being enabled. Larger chunk sizes can give
better compression ratio but at the same time use more RAM.

