#!/bin/sh

case $# in
0)	echo >&2 Usage: $0 "--prefix=<DIR> [--with-pip=<PIP_DIR> --with-glibc-libdir=<GLIBC_LIBDIR>]"
	exit 1
	;;
esac

: ${BUILD_PARALLELISM=`getconf _NPROCESSORS_ONLN`}

make clean
make distclean
find . -name config.cache -delete

./configure \
	--enable-gdb-build-warnings=,-Wno-unused \
	--enable-werror \
	--with-separate-debug-dir=/usr/lib/debug \
	--disable-sim \
	--disable-rpath \
	--with-system-readline \
	--with-expat \
	--without-libexpat-prefix \
	--enable-tui \
	--with-rpm \
	--with-lzma \
	--without-libunwind \
	--enable-64-bit-bfd \
	--enable-inprocess-agent \
	--with-auto-load-dir='$debugdir:$datadir/auto-load' \
	--with-auto-load-safe-path='$debugdir:$datadir/auto-load:/usr/bin/mono-gdb.py' \
	--enable-targets=s390-linux-gnu,powerpc-linux-gnu,powerpcle-linux-gnu \
	"$@" \
	x86_64-redhat-linux-gnu \
    &&

make -j ${BUILD_PARALLELISM} "CFLAGS=-O2 -g -pipe -Wall \
	-Wp,-D_FORTIFY_SOURCE=2 \
	-fexceptions \
	-fstack-protector-strong \
	--param=ssp-buffer-size=4 \
	-grecord-gcc-switches \
	-m64 \
	-mtune=generic" \
	"LDFLAGS=-Wl,-z,relro" \
	maybe-configure-gdb \
    &&

perl -i.relocatable -pe 's/^(D\[".*_RELOCATABLE"\]=" )1(")$/${1}0$2/' \
	gdb/config.status \
    &&

make -j ${BUILD_PARALLELISM} "CFLAGS=-O2 -g -pipe -Wall \
	-Wp,-D_FORTIFY_SOURCE=2 \
	-fexceptions \
	-fstack-protector-strong \
	--param=ssp-buffer-size=4 \
	-grecord-gcc-switches \
	-m64 \
	-mtune=generic" \
	"LDFLAGS=-Wl,-z,relro" \
    &&

make install
