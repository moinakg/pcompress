Pcompress
=========

Copyright (C) 2012 Moinak Ghosh. All rights reserved.
Use is subject to license terms.

Pcompress is a utility to do compression and decompression in parallel by
splitting input data into chunks. It has a modular structure and includes
support for multiple algorithms like LZMA, Bzip2, PPMD, etc., with CRC64
chunk checksums. SSE optimizations for the bundled LZMA are included. It
also implements chunk-level Content-Aware Deduplication and Delta
Compression features based on a Semi-Rabin Fingerprinting scheme. Delta
Compression is implemented via the widely popular bsdiff algorithm.
Similarity is detected using a custom hashing of maximal features of a
block. When doing chunk-level dedupe it attempts to merge adjacent
non-duplicate blocks index entries into a single larger entry to reduce
metadata. In addition to all these it can internally split chunks at
rabin boundaries to help dedupe and compression.

It has low metadata overhead and overlaps I/O and compression to achieve
maximum parallelism. It also bundles a simple slab allocator to speed
repeated allocation of similar chunks. It can work in pipe mode, reading
from stdin and writing to stdout. It also provides some adaptive compression
modes in which multiple algorithms are tried per chunk to determine the best
one for the given chunk. Finally it support 14 compression levels to allow
for ultra compression modes in some algorithms.

Usage
=====

    To compress a file:
       pcompress -c <algorithm> [-l <compress level>] [-s <chunk size>] <file>
       Where <algorithm> can be the folowing:
       lzfx   - Very fast and small algorithm based on LZF.
       lz4    - Ultra fast, high-throughput algorithm reaching RAM B/W at level1.
       zlib   - The base Zlib format compression (not Gzip).
       lzma   - The LZMA (Lempel-Ziv Markov) algorithm from 7Zip.
       bzip2  - Bzip2 Algorithm from libbzip2.
       ppmd   - The PPMd algorithm excellent for textual data. PPMd requires
                at least 64MB X CPUs more memory than the other modes.
       adapt  - Adaptive mode where ppmd or bzip2 will be used per chunk,
                depending on which one produces better compression. This mode
                is obviously fairly slow and requires lots of memory.
       adapt2 - Adaptive mode which includes ppmd and lzma. This requires
                more memory than adapt mode, is slower and potentially gives
                the best compression.
       <chunk_size> - This can be in bytes or can use the following suffixes:
                g - Gigabyte, m - Megabyte, k - Kilobyte.
                Larger chunks produce better compression at the cost of memory.
       <compress_level> - Can be a number from 0 meaning minimum and 14 meaning
                maximum compression.

    To decompress a file compressed using above command:
       pcompress -d <compressed file> <target file>

    To operate as a pipe, read from stdin and write to stdout:
       pcompress -p ...

    Attempt Rabin fingerprinting based deduplication on chunks:
       pcompress -D ...
       pcompress -D -r ... - Do NOT split chunks at a rabin boundary. Default is to split.

    Perform Delta Encoding in addition to Exact Dedup:
       pcompress -E ... - This also implies '-D'.

    Number of threads can optionally be specified: -t <1 - 256 count>
    Pass '-M' to display memory allocator statistics
    Pass '-C' to display compression statistics

Examples
========

Compress "file.tar" using bzip2 level 6, 64MB chunk size and use 4 threads. In
addition perform exact deduplication and delta compression prior to compression.

    pcompress -D -E -c bzip2 -l6 -s64m -t4 file.tar

Compress "file.tar" using extreme compression mode of LZMA and a chunk size of
of 1GB. Allow pcompress to detect the number of CPU cores and use as many threads.

    pcompress -c lzma -l14 -s1g file.tar

