Pcompress
=========

Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
Use is subject to license terms.
moinakg (_at) gma1l _dot com.
Comments, suggestions, code, rants etc are welcome.

Pcompress is an archiver that also does compression and decompression in
parallel by splitting input data into chunks. It has a modular structure
and includes support for multiple algorithms like LZMA, Bzip2, PPMD, etc,
with SKEIN/SHA checksums for data integrity. Compression algorithms are
selected based on the file type to maximize compression gains using a file
and data anaylis based adaptive technique. It also includes various data
transformation filters to improve compression.

It also implements Variable Block Deduplication and Delta Compression
features based on a Polynomial Fingerprinting scheme. Delta Compression
is done via the widely popular bsdiff algorithm. Similarity is detected
using a technique based on MinHashing. Deduplication metadata is also
compressed to reduce overheads. In addition to all these it can internally
split chunks at file and rabin boundaries to help Dedupe and compression.

It has low metadata overhead and overlaps I/O and compression to achieve
maximum parallelism. It also bundles a simple slab allocator to speed
repeated allocation of similar chunks. It can work in pipe mode, reading
from stdin and writing to stdout. SIMD vector optimizations using the x86
SSE instruction set are used to speed up various operations. Finally it
supports 14 compression levels to allow for ultra compression parameters
in some algorithms.

Pcompress also supports encryption via AES, Salsa20 and uses Scrypt from
Tarsnap for Password Based Key generation. A unique key is generated per
session even if the same password is used and HMAC is used to do authentication.

LICENSING
=========

Pcompress is dual licensed with LGPLv3 and MPLv2 sources. The main git
repository is LGPL licensed. A separate tarball of the sources with
MPLv2 license is made available as a download. This is updated
periodically. Since Pcompress also integrates a bunch of third-party
software a few features may be missing in the MPLv2 licensed version
because of the upstream software being LGPL licensed originally.

Links of Interest
=================

Project Home Page: http://moinakg.github.io/pcompress/

http://moinakg.github.io/pcompress/#deduplication-chunking-analysis

http://moinakg.github.io/pcompress/#compression-benchmarks

http://moinakg.wordpress.com/2013/04/26/pcompress-2-0-with-global-deduplication/

http://moinakg.wordpress.com/2013/03/26/coordinated-parallelism-using-semaphores/

http://moinakg.wordpress.com/2013/06/11/architecture-for-a-deduplicated-archival-store-part-1/

http://moinakg.wordpress.com/2013/06/15/architecture-for-a-deduplicated-archival-store-part-2/

Standard Usage
==============
    Standard usage consists of a few common options to control basic behavior. A variety of
    parameters including global deduplication are automatically set based on the compression
    level.

    Archiving
    ---------
       pcompress -a [-v] [-l <compress level>] [-s <chunk size>] [-c <algorithm>]
                    [<file1> <directory1> <file2> ...] [-t <number>] [-S <chunk checksum>]
                    <archive filename or '-'>

       Archives a given set of files and/or directories into a compressed PAX archive. The
       PAX datastream is encoded into a custom format compressed file that can only be
       handled by Pcompress.

       -a       Enables archive mode where pathnames specified in the command line are
                archived using LibArchive and then compressed.

       -l <compress level>
                Select a compression level from 1 (least compression, fastest) to 14
                (ultra compression, slow). Default: 6

       -s <chunk size>
                Archive data is split into chunks that are processed in parallel. This value
                specifies the maximum chunk size. Blocks may be smaller than this. Values
                can be in bytes or <number><suffix> format where suffix can be k - KB, m - MB,
                g - GB. Default: 8m
                Larger chunks can produce better compression at the cost of memory.

       -c <algorithm>
                Specifies the compression algorithm to use. Default algorithm when archiving
                is adapt2 (Second Adaptive Mode). This is the ideal mode for archiving giving
                best compression gains. However adapt (Adaptive Mode) can be used which is a
                little faster but give lower compression gains.
                Other algorithms can be used if all the files are of the same known type. For
                example ppmd (slow) or libbsc (fast) can be used if all the files only have
                ASCII text. See section "Compression Algorithms" for details.

       -v       Enables verbose mode where each file/directory is printed as it is processed.

       -t <number>
                Sets the number of threads that Pcompress can use. Pcompress automatically
                uses thread count = core count. However with larger chunk size (-s option)
                and/or ultra compression levels, large amounts of memory can be used. In this
                case thread count can be reduced to reduce memory consumption.

       -S <chunk checksum>
                Specify then chunk checksum to use. Default: BLAKE256. The following checksums
                are available:

                     CRC64 - Extremely Fast 64-bit CRC from LZMA SDK.
                    SHA256 - SHA512/256 version of Intel's optimized (SSE,AVX) SHA2 for x86.
                    SHA512 - SHA512 version of Intel's optimized (SSE,AVX) SHA2 for x86.
                 KECCAK256 - Official 256-bit NIST SHA3 optimized implementation.
                 KECCAK512 - Official 512-bit NIST SHA3 optimized implementation.
                  BLAKE256 - Very fast 256-bit BLAKE2, derived from the NIST SHA3
                             runner-up BLAKE.
                  BLAKE512 - Very fast 256-bit BLAKE2, derived from the NIST SHA3
                             runner-up BLAKE.

                 The fastest checksum is the BLAKE2 family.

       -T
                Disable Metadata Streams. Pathname metadata is normally packed into separate
                chunks distinct from file data. With this option this behavior is disabled.

       <archive filename>
                Pathname of the resulting archive. A '.pz' extension is automatically added
                if not already present. This can also be specified as '-' in order to send
                the compressed archive stream to stdout.

    Single File Compression
    -----------------------
       pcompress -c <algorithm> [-l <compress level>] [-s <chunk size>] [-p] [<file>]
                 [-t <number>] [-S <chunk checksum>] [<target file or '-'>]

       Takes a single file as input and produces a compressed file. Archiving is not performed.
       This can also work in streaming mode.

       -c <algorithm>
                See above. Also see section "Compression Algorithms" for details.

       -l <compress level>
       -s <chunk size>
       -t <number>
       -S <chunk checksum>
                See above.
                Note: In singe file compression mode with adapt2 or adapt algorithm, larger
                      chunks may not produce better compression. Smaller chunks can result
                      in better data analysis here.

       -p       Make Pcompress work in streaming mode. Data is ingested via stdin
                compressed and output via stdout. No filenames are used.

       <target file>
                Pathname of the compressed file to be created. This can be '-' to send the
                compressed data to stdout.

    Decompression and Archive extraction
    ------------------------------------
       pcompress -d <compressed file or '-'> [-m] [-K] [-i] [<target file or directory>]

       -m        Enable restoring *all* permissions, ACLs, Extended Attributes etc.
                 Equivalent to the '-p' option in tar. Ownership is only extracted if run as
                 root user.
       -K        Do not overwrite newer files.
       -i        Only list contents of the archive, do not extract.

       -m and -K are only meaningful if the compressed file is an archive. For single file
       compressed mode these options are ignored.

       <compressed file>
                Specifies the compressed file or archive. This can be '-' to indicate reading
                from stdin while write goes to <target file>

       <target file or directory>
                This can be a filename or a directory depending on how the archive was created.
                If single file compression was used then this can be the name of the target
                file that will hold the uncompressed data.
                If this is omitted then an output file is created by appending '.out' to the
                compressed filename.

                If Archiving was done then this should be the name of a directory into which
                extracted files are restored. The directory is created if it does not exist.
                If this is omitted the files are extracted into the current directory.

Compression Algorithms
======================

    lzfx    - Fast, average compression. At high compression levels this can be faster
              than LZ4.
              Effective Levels: 1 - 5
    lz4     - Very Fast, sometimes better compression than LZFX.
              Effective Levels: 1 - 3
    zlib    - Fast, better compression.
              Effective Levels: 1 - 9
    bzip2   - Slow, much better compression than Zlib.
              Effective Levels: 1 - 9

    lzma    - Very slow. Extreme compression. Recommended: Use lzmaMt variant mentioned
              below.
              Effective Levels: 1 - 14
              Till level 9 it is standard LZMA parameters. Levels 10 - 12 use
              more memory and higher match iterations so are slower. Levels
              13 and 14 use larger dictionaries upto 256MB and really suck up
              RAM. Use these levels only if you have at the minimum 4GB RAM on
              your system.
    lzmaMt  - This is the multithreaded variant of lzma and typically runs faster.
              However in a few cases this can produce slightly lesser compression
              gain.

    libbsc  - This is a new block-sorting compressor having much better effectiveness
              and performance over a variety of data types as compared to Bzip2.
    NOTE:     In the LGPL licensed version libbsc is an integral part of Pcompress.
              When building MPLv2 licensed sources, the libbsc sources must be
              downloaded separately and linked in. This is described in the INSTALL file.

    PPMD    - Slow. Extreme compression for Text, average compression for binary.
              In addition PPMD decompression time is also high for large chunks.
              This requires lots of RAM similar to LZMA. PPMd requires
              at least 64MB X core-count more memory than the other modes.
              Effective Levels: 1 - 14.

    Adapt   - Synthetic mode with text/binary detection. For pure text data PPMD is
              used otherwise Bzip2 is selected per chunk.
              Effective Levels: 1 - 14
    Adapt2  - Slower synthetic mode. For pure text data PPMD is otherwise LZMA is
              applied. Can give very good compression ratio when splitting file
              into multiple chunks.
              Effective Levels: 1 - 14
              Since both LZMA and PPMD are used together memory requirements are
              large especially if you are also using extreme levels above 10. For
              example with 100MB chunks, Level 14, 2 threads and with or without
              dedupe, it uses upto 2.5GB physical RAM (RSS).

    none    - No compression. This is only meaningful with -G or -D. So Dedupe
              can be done for post-processing with an external utility.

Enabled features based on Compression Level
===========================================

    1 to 3  - No features, just compression and archiving, if needed.
    4       - Global Deduplication with avg block size of 8KB.
    5       - Global Dedup block size 8KB, Adaptive Delta Encoding.
    6 to 8  - Global Dedup block size reduced to 4KB, Adaptive Delta Encoding.
    9       - Global Dedup block size reduced to 2KB, Adaptive Delta Encoding, Dispack.
    10      - Global Dedup block size 2KB, Adaptive Delta Encoding with extra rounds, Dispack,
              LZP Preprocessing
    10 - 14 - Global Dedup block size 2KB, Adaptive Delta Encoding with extra rounds, Dispack,
              LZP Preprocessing, PackJPG filter for Jpegs.

    NOTE:   - LZP Preprocessing and PackJPG are not available in the MPLv2 licensed version.

Encryption
==========
    Pcompress supports encryption and authentication in both archive and single-file
    compresion modes. Encryption options are discussed below.

    NOTE: When using pipe-mode via -p the only way to provide a password is to use '-w'.
          See below.

       -e <ALGO>
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

       -w <pathname>
                Provide a file which contains the encryption password. This file must
                be readable and writable since it is zeroed out after the password is
                read.

       -k <key length>
                Specify the key length. Can be 16 for 128 bit keys or 32 for 256 bit
                keys. Default value is 32 for 256 bit keys.


Advanced usage
==============
    A variety of advanced options are provided if one wishes fine grained control
    as opposed to automatic settings. If advanced options are used then auto-setting
    of parameters get disabled. The various advanced options are discussed below.

    Chunk-level Deduplication
    -------------------------
    Attempt Polynomial fingerprinting based deduplication on a per-chunk basis:
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

       -F       Perform Fixed Block Deduplication. This is faster than fingerprinting
                based content-aware deduplication in some cases. However this is mostly
                usable for disk dumps especially virtual machine images. This generally
                gives lower dedupe ratio than content-aware dedupe (-D) and does not
                support delta compression.

    Global Deduplication
    --------------------
       -G       This flag enables Global Deduplication. This makes pcompress maintain an
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

       -B <0..5>
                Specify an average Dedupe block size. 0 - 2K, 1 - 4K, 2 - 8K ... 5 - 64K.
                Default deduplication block size is 4KB for Global Deduplication and 2KB
                otherwise.
       -B 0
                This uses blocks as small as 2KB for deduplication. This option can be
                used for datasets of a few GBs to a few hundred TBs in size depending on
                available RAM.

       -L       Enable LZP pre-compression. This improves compression ratio of all
                algorithms with some extra CPU and very low RAM overhead. Using
                delta encoding in conjunction with this may not always be beneficial.
                However Adaptive Delta Encoding is beneficial along with this.

       -P       Enable Adaptive Delta Encoding. It can improve compresion ratio further
                for data containing tables of numerical values especially if those are
                in an arithmetic series. In this implementation basic Delta Encoding is
                combined with Run-Length encoding and Matrix transpose
       NOTE -   Both -L and -P can be used together to give maximum benefit on most
                datasets.

       -x       Perform Dispack Encoding. This is useful to translate x86 call and jmp
                relative offsets to absolute values which compress better. The given
                chunk is split into 32KB blocks and some heuristics are used per block
                to identify whether it represents x86 instruction stream or not. This
                works only when archiving.

       -j       Enable PackJPG processing for Jpeg files. This works only when archiving.

       -M       Display memory allocator statistics.
       -C       Display compression statistics.
       -CC      Display compression statistics and print the offset and length of each
                variable length dedupe block if variable block deduplication is being
                used. This has no effect for fixed block deduplication.

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

Archive contents of directory /usr/include into usr.pz. Default chunk or per-thread
segment size is 8MB and default compression level is 6.

    pcompress -a /usr/include usr

Archive the given listr of files into file.pz and max compresion level and all features
enabled. A maximum chunk size of 20MB is used. Also use verbose mode which lists each
file as it is processed.

    pcompress -a -v -l14 -s20m file1 file2 file3 file

Simple compress "file.tar" using zlib(gzip) algorithm. Default chunk or per-thread
segment size is 8MB and default compression level is 6. Output file created will be
file.tar.pz

    pcompress -c zlib file.tar

Simple compress "file.tar" using zlib(gzip) algorithm with output file file.compressed.pz

    pcompress -c zlib file.tar file.compressed

Compress "file.tar" using Zlib and per-thread chunk or segment size of 10MB and
Compression level 9. Compressed output is sent to stdout using '-' which is then
redirected to a file.

    pcompress -c zlib -l9 -s10m file.tar - > /path/to/compress_file.tar.pz

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

    2) Delta Compression : A similarity based (minhash) comparison of Rabin blocks.
                           Two blocks at least 60% similar with each other are diffed
                           using bsdiff.

    3) LZP               : LZ Prediction is a variant of LZ77 that replaces repeating
                           runs of text with shorter codes.

    4) Adaptive Delta    : This is a simple form of Delta Encoding where arithmetic
                           progressions are detected in the data stream and
                           collapsed via Run-Length encoding.

    4) Matrix Transpose  : This is used automatically in Delta Encoding and
                           Deduplication. This attempts to transpose columnar
                           repeating sequences of bytes into row-wise sequences so
                           that compression algorithms can work better.

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


