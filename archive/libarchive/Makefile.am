## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS= foreign subdir-objects
ACLOCAL_AMFLAGS = -I build/autoconf

#
# What to build and install
#
lib_LTLIBRARIES=	libarchive.la
noinst_LTLIBRARIES=	libarchive_fe.la
bin_PROGRAMS=	$(bsdtar_programs) $(bsdcpio_programs)
man_MANS=
BUILT_SOURCES= 

#
# What to test: We always test libarchive, test bsdtar and bsdcpio only
# if we built them.
#
check_PROGRAMS= 
TESTS=
TESTS_ENVIRONMENT=
# Always build and test both bsdtar and bsdcpio as part of 'distcheck'
DISTCHECK_CONFIGURE_FLAGS = --enable-bsdtar --enable-bsdcpio
COMMON_CFLAGS=-Wall -Wformat -Wformat-security
# The next line is commented out by default in shipping libarchive releases.
# It is uncommented by default in trunk.
# DEV_CFLAGS=-Werror -Wextra -Wunused -Wshadow -Wmissing-prototypes -Wcast-qual
AM_CFLAGS=$(COMMON_CFLAGS) $(DEV_CFLAGS)
PLATFORMCPPFLAGS = @PLATFORMCPPFLAGS@
AM_CPPFLAGS=$(PLATFORMCPPFLAGS)

#
# What to include in the distribution
#
EXTRA_DIST= \
	CMakeLists.txt			\
	build/autogen.sh		\
	build/bump-version.sh		\
	build/clean.sh			\
	build/cmake			\
	build/version			\
	contrib				\
	doc				\
	examples			\
	$(libarchive_EXTRA_DIST)	\
	$(libarchive_test_EXTRA_DIST)	\
	$(bsdtar_EXTRA_DIST)		\
	$(bsdtar_test_EXTRA_DIST)	\
	$(bsdcpio_EXTRA_DIST)		\
	$(bsdcpio_test_EXTRA_DIST)

# a) Clean out some unneeded files and directories
# b) Collect all documentation and format it for distribution.
dist-hook:
	rm -rf `find $(distdir) -name CVS -type d`
	rm -rf `find $(distdir) -name .svn -type d`
	rm -f `find $(distdir) -name '*~'`
	rm -f `find $(distdir) -name '*.out'`
	rm -f `find $(distdir) -name '*.core'`
	-rm -f $(distdir)/*/Makefile $(distdir)/*/*/Makefile
	cd $(distdir)/doc && /bin/sh update.sh

#
# Extra rules for cleanup
#
DISTCLEANFILES=					\
	libarchive/test/list.h			\
	tar/test/list.h				\
	cpio/test/list.h

distclean-local:
	-rm -rf .ref
	-rm -rf autom4te.cache/
	-rm -f *~
	-[ -f libarchive/Makefile ] && cd libarchive && make clean
	-[ -f libarchive/test/Makefile ] && cd libarchive/test && make clean
	-[ -f tar/Makefile ] && cd tar && make clean
	-[ -f tar/test/Makefile ] && cd tar/test && make clean
	-[ -f cpio/Makefile ] && cd cpio && make clean
	-[ -f cpio/test/Makefile ] && cd cpio/test && make clean

#
# Libarchive headers, source, etc.
#
#

include_HEADERS= libarchive/archive.h libarchive/archive_entry.h

libarchive_la_SOURCES=						\
	libarchive/archive_acl.c				\
	libarchive/archive_acl_private.h			\
	libarchive/archive_check_magic.c			\
	libarchive/archive_cmdline.c				\
	libarchive/archive_cmdline_private.h			\
	libarchive/archive_crc32.h				\
	libarchive/archive_crypto.c				\
	libarchive/archive_crypto_private.h			\
	libarchive/archive_endian.h				\
	libarchive/archive_entry.c				\
	libarchive/archive_entry.h				\
	libarchive/archive_entry_copy_stat.c			\
	libarchive/archive_entry_link_resolver.c		\
	libarchive/archive_entry_locale.h			\
	libarchive/archive_entry_private.h			\
	libarchive/archive_entry_sparse.c			\
	libarchive/archive_entry_stat.c				\
	libarchive/archive_entry_strmode.c			\
	libarchive/archive_entry_xattr.c			\
	libarchive/archive_getdate.c				\
	libarchive/archive_match.c				\
	libarchive/archive_options.c				\
	libarchive/archive_options_private.h			\
	libarchive/archive_pathmatch.c				\
	libarchive/archive_pathmatch.h				\
	libarchive/archive_platform.h				\
	libarchive/archive_ppmd_private.h			\
	libarchive/archive_ppmd7.c				\
	libarchive/archive_ppmd7_private.h			\
	libarchive/archive_private.h				\
	libarchive/archive_rb.c					\
	libarchive/archive_rb.h					\
	libarchive/archive_read.c				\
	libarchive/archive_read_append_filter.c			\
	libarchive/archive_read_data_into_fd.c			\
	libarchive/archive_read_disk_entry_from_file.c		\
	libarchive/archive_read_disk_posix.c			\
	libarchive/archive_read_disk_private.h			\
	libarchive/archive_read_disk_set_standard_lookup.c	\
	libarchive/archive_read_extract.c			\
	libarchive/archive_read_open_fd.c			\
	libarchive/archive_read_open_file.c			\
	libarchive/archive_read_open_filename.c			\
	libarchive/archive_read_open_memory.c			\
	libarchive/archive_read_private.h			\
	libarchive/archive_read_set_format.c			\
	libarchive/archive_read_set_options.c			\
	libarchive/archive_read_support_filter_all.c		\
	libarchive/archive_read_support_filter_bzip2.c		\
	libarchive/archive_read_support_filter_compress.c	\
	libarchive/archive_read_support_filter_grzip.c		\
	libarchive/archive_read_support_filter_gzip.c		\
	libarchive/archive_read_support_filter_lrzip.c		\
	libarchive/archive_read_support_filter_lzop.c		\
	libarchive/archive_read_support_filter_none.c		\
	libarchive/archive_read_support_filter_program.c	\
	libarchive/archive_read_support_filter_rpm.c		\
	libarchive/archive_read_support_filter_uu.c		\
	libarchive/archive_read_support_filter_xz.c		\
	libarchive/archive_read_support_format_7zip.c		\
	libarchive/archive_read_support_format_all.c		\
	libarchive/archive_read_support_format_ar.c		\
	libarchive/archive_read_support_format_by_code.c	\
	libarchive/archive_read_support_format_cab.c		\
	libarchive/archive_read_support_format_cpio.c		\
	libarchive/archive_read_support_format_empty.c		\
	libarchive/archive_read_support_format_iso9660.c	\
	libarchive/archive_read_support_format_lha.c		\
	libarchive/archive_read_support_format_mtree.c		\
	libarchive/archive_read_support_format_rar.c		\
	libarchive/archive_read_support_format_raw.c		\
	libarchive/archive_read_support_format_tar.c		\
	libarchive/archive_read_support_format_xar.c		\
	libarchive/archive_read_support_format_zip.c		\
	libarchive/archive_string.c				\
	libarchive/archive_string.h				\
	libarchive/archive_string_composition.h			\
	libarchive/archive_string_sprintf.c			\
	libarchive/archive_util.c				\
	libarchive/archive_virtual.c				\
	libarchive/archive_write.c				\
	libarchive/archive_write_disk_acl.c			\
	libarchive/archive_write_disk_posix.c			\
	libarchive/archive_write_disk_private.h			\
	libarchive/archive_write_disk_set_standard_lookup.c	\
	libarchive/archive_write_open_fd.c			\
	libarchive/archive_write_open_file.c			\
	libarchive/archive_write_open_filename.c		\
	libarchive/archive_write_open_memory.c			\
	libarchive/archive_write_private.h			\
	libarchive/archive_write_add_filter.c			\
	libarchive/archive_write_add_filter_b64encode.c		\
	libarchive/archive_write_add_filter_by_name.c		\
	libarchive/archive_write_add_filter_bzip2.c		\
	libarchive/archive_write_add_filter_compress.c		\
	libarchive/archive_write_add_filter_grzip.c		\
	libarchive/archive_write_add_filter_gzip.c		\
	libarchive/archive_write_add_filter_lrzip.c		\
	libarchive/archive_write_add_filter_lzop.c		\
	libarchive/archive_write_add_filter_none.c		\
	libarchive/archive_write_add_filter_program.c		\
	libarchive/archive_write_add_filter_uuencode.c		\
	libarchive/archive_write_add_filter_xz.c		\
	libarchive/archive_write_set_format.c			\
	libarchive/archive_write_set_format_7zip.c		\
	libarchive/archive_write_set_format_ar.c		\
	libarchive/archive_write_set_format_by_name.c		\
	libarchive/archive_write_set_format_cpio.c		\
	libarchive/archive_write_set_format_cpio_newc.c		\
	libarchive/archive_write_set_format_iso9660.c		\
	libarchive/archive_write_set_format_mtree.c		\
	libarchive/archive_write_set_format_pax.c		\
	libarchive/archive_write_set_format_shar.c		\
	libarchive/archive_write_set_format_ustar.c		\
	libarchive/archive_write_set_format_v7tar.c		\
	libarchive/archive_write_set_format_gnutar.c		\
	libarchive/archive_write_set_format_xar.c		\
	libarchive/archive_write_set_format_zip.c		\
	libarchive/archive_write_set_options.c			\
	libarchive/config_freebsd.h				\
	libarchive/filter_fork_posix.c				\
	libarchive/filter_fork.h

if INC_WINDOWS_FILES
libarchive_la_SOURCES+=						\
	libarchive/archive_entry_copy_bhfi.c			\
	libarchive/archive_read_disk_windows.c			\
	libarchive/archive_windows.h				\
	libarchive/archive_windows.c				\
	libarchive/archive_write_disk_windows.c			\
	libarchive/filter_fork_windows.c
endif

# -no-undefined marks that libarchive doesn't rely on symbols
# defined in the application.  This is mandatory for cygwin.
libarchive_la_LDFLAGS= -no-undefined -version-info $(ARCHIVE_LIBTOOL_VERSION)
libarchive_la_LIBADD= $(LTLIBICONV)

# Manpages to install
libarchive_man_MANS=

# Additional libarchive files to include in the distribution
libarchive_EXTRA_DIST=			\
	libarchive/archive_windows.c	\
	libarchive/archive_windows.h	\
	libarchive/filter_fork_windows.c	\
	libarchive/CMakeLists.txt	\
	$(libarchive_man_MANS)

# pkgconfig
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = build/pkgconfig/libarchive.pc

# Sources needed by all test programs
test_utils_SOURCES=

#
#
# libarchive_test program
#
#
libarchive_test_SOURCES=

libarchive_test_CPPFLAGS= -I$(top_srcdir)/libarchive -I$(top_srcdir)/test_utils -I$(top_builddir)/libarchive/test -DLIBARCHIVE_STATIC $(PLATFORMCPPFLAGS)
libarchive_test_LDADD= $(LTLIBICONV)

libarchive_TESTS_ENVIRONMENT=

libarchive_test_EXTRA_DIST=

#
# Common code for libarchive frontends (cpio, tar)
#
libarchive_fe_la_SOURCES=			\
	libarchive_fe/err.c			\
	libarchive_fe/err.h			\
	libarchive_fe/lafe_platform.h		\
	libarchive_fe/line_reader.c		\
	libarchive_fe/line_reader.h

libarchive_fe_la_CPPFLAGS= -I$(top_srcdir)/libarchive
#
#
# bsdtar source, docs, etc.
#
#

bsdtar_SOURCES=				\
		tar/bsdtar.c		\
		tar/bsdtar.h		\
		tar/bsdtar_platform.h	\
		tar/cmdline.c		\
		tar/creation_set.c	\
		tar/read.c		\
		tar/subst.c		\
		tar/util.c		\
		tar/write.c

if INC_WINDOWS_FILES
bsdtar_SOURCES+=			\
		tar/bsdtar_windows.h	\
		tar/bsdtar_windows.c
endif

bsdtar_DEPENDENCIES= libarchive.la libarchive_fe.la

if STATIC_BSDTAR
bsdtar_ldstatic= -static
bsdtar_ccstatic= -DLIBARCHIVE_STATIC
else
bsdtar_ldstatic=
bsdtar_ccstatic=
endif

bsdtar_LDADD= libarchive.la libarchive_fe.la $(LTLIBICONV)
bsdtar_CPPFLAGS= -I$(top_srcdir)/libarchive -I$(top_srcdir)/libarchive_fe $(bsdtar_ccstatic) $(PLATFORMCPPFLAGS)
bsdtar_LDFLAGS= $(bsdtar_ldstatic)

bsdtar_EXTRA_DIST=		\
	tar/bsdtar.1		\
	tar/bsdtar_windows.h	\
	tar/bsdtar_windows.c	\
	tar/CMakeLists.txt	\
	tar/config_freebsd.h


if BUILD_BSDTAR
bsdtar_man_MANS= tar/bsdtar.1
bsdtar_programs= bsdtar
else
bsdtar_man_MANS=
bsdtar_programs=
endif

#
# bsdtar_test
#

bsdtar_test_SOURCES=

bsdtar_test_CPPFLAGS=\
	-I$(top_srcdir)/libarchive -I$(top_srcdir)/libarchive_fe \
	-I$(top_srcdir)/test_utils \
	-I$(top_srcdir)/tar -I$(top_builddir)/tar/test \
	$(PLATFORMCPPFLAGS)

if BUILD_BSDTAR
bsdtar_test_programs=
bsdtar_TESTS_ENVIRONMENT= BSDTAR=`cd $(top_builddir);/bin/pwd`/bsdtar$(EXEEXT) BSDTAR_TEST_FILES=`cd $(top_srcdir);/bin/pwd`/tar/test
else
bsdtar_test_programs=
bsdtar_TESTS_ENVIRONMENT=
endif

bsdtar_test_EXTRA_DIST=	

#
#
# bsdcpio source, docs, etc.
#
#

bsdcpio_SOURCES=			\
		cpio/cmdline.c		\
		cpio/cpio.c		\
		cpio/cpio.h		\
		cpio/cpio_platform.h

if INC_WINDOWS_FILES
bsdcpio_SOURCES+=			\
		cpio/cpio_windows.h	\
		cpio/cpio_windows.c
endif

bsdcpio_DEPENDENCIES = libarchive.la libarchive_fe.la


if STATIC_BSDCPIO
bsdcpio_ldstatic= -static
bsdcpio_ccstatic= -DLIBARCHIVE_STATIC
else
bsdcpio_ldstatic=
bsdcpio_ccstatic=
endif

bsdcpio_LDADD= libarchive_fe.la libarchive.la $(LTLIBICONV)
bsdcpio_CPPFLAGS= -I$(top_srcdir)/libarchive -I$(top_srcdir)/libarchive_fe $(bsdcpio_ccstatic) $(PLATFORMCPPFLAGS)
bsdcpio_LDFLAGS= $(bsdcpio_ldstatic)

bsdcpio_EXTRA_DIST=		\
	cpio/bsdcpio.1		\
	cpio/cpio_windows.h	\
	cpio/cpio_windows.c	\
	cpio/CMakeLists.txt	\
	cpio/config_freebsd.h


if BUILD_BSDCPIO
# Manpages to install
bsdcpio_man_MANS=
bsdcpio_programs= bsdcpio
else
bsdcpio_man_MANS=
bsdcpio_programs=
endif

#
# bsdcpio_test
#

bsdcpio_test_SOURCES=

bsdcpio_test_CPPFLAGS= \
	-I$(top_srcdir)/libarchive -I$(top_srcdir)/libarchive_fe \
	-I$(top_srcdir)/test_utils \
	-I$(top_srcdir)/cpio -I$(top_builddir)/cpio/test \
	$(PLATFORMCPPFLAGS)
bsdcpio_test_LDADD=libarchive_fe.la

if BUILD_BSDCPIO
bsdcpio_test_programs= 
bsdcpio_TESTS_ENVIRONMENT= BSDCPIO=`cd $(top_builddir);/bin/pwd`/bsdcpio$(EXEEXT) BSDCPIO_TEST_FILES=`cd $(top_srcdir);/bin/pwd`/cpio/test
else
bsdcpio_test_programs=
bsdcpio_TESTS_ENVIRONMENT=
endif

bsdcpio_test_EXTRA_DIST=
