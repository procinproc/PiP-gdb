#!/bin/sh

# $RIKEN_copyright: 2018 Riken Center for Computational Sceience, 
# 	  System Software Devlopment Team. All rights researved$
#
# This file is part of GDB.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

usage()
{
	echo >&2 "Usage: `basename $0` [-b [-k]] --prefix=<DIR> [--with-pip=<PIP_DIR> --with-glibc-libdir=<GLIBC_LIBDIR>]"
	echo >&2 "       `basename $0`  -i"
#	echo >&2 "	-B      : build only, do not install, do not clean"
	echo >&2 "	-b      : build only, do not install"
	echo >&2 "	-i      : install only, do not build"
	exit 2
}

do_clean=true
do_build=true
do_install=true

: ${BUILD_PARALLELISM=`getconf _NPROCESSORS_ONLN`}
: ${EXTRA_CONFIGURE_OPTIONS="--enable-werror --with-rpm --enable-targets=s390-linux-gnu,powerpc-linux-gnu,powerpcle-linux-gnu"}
: ${EXTRA_GCC_OPTIONS="-fstack-protector-strong -grecord-gcc-switches"}

program_prefix=
case $@ in
*--with-pip*)
	program_prefix=--program-prefix=pip-
	;;
esac

# -B -b, and -i have to be first option.
case "$1" in
-B)	do_install=false; do_clean=false; shift;;
-b)	do_install=false; shift;;
-i)	do_build=false; shift;;
esac

if $do_build; then
	case $# in
	0)	usage;;
	*)	:;;
	esac
else
	case $# in
	0)	:;;
	*)	usage;;
	esac
fi

set -x

if $do_build; then

	if $do_clean; then
		make clean
		make distclean
		find . -name config.cache -delete
	fi

	./configure \
		--enable-gdb-build-warnings=,-Wno-unused \
		--with-separate-debug-dir=/usr/lib/debug \
		--disable-sim \
		--disable-rpath \
		--with-system-readline \
		--with-expat \
		--without-libexpat-prefix \
		--enable-tui \
		--without-python \
		--with-lzma \
		--without-libunwind \
		--enable-64-bit-bfd \
		--enable-inprocess-agent \
		--with-auto-load-dir='$debugdir:$datadir/auto-load' \
		--with-auto-load-safe-path='$debugdir:$datadir/auto-load:/usr/bin/mono-gdb.py' \
		${EXTRA_CONFIGURE_OPTIONS} \
		"$@" ${program_prefix} \
		x86_64-redhat-linux-gnu \
	    &&

	make -j ${BUILD_PARALLELISM} "CFLAGS=-O2 -g -pipe -Wall \
		-Wp,-D_FORTIFY_SOURCE=2 \
		-fexceptions \
		--param=ssp-buffer-size=4 \
		-m64 \
		-mtune=generic \
		${EXTRA_GCC_OPTIONS}" \
		"LDFLAGS=-Wl,-z,relro" \
		maybe-configure-gdb \
	    &&

	perl -i.relocatable -pe 's/^(D\[".*_RELOCATABLE"\]=" )1(")$/${1}0$2/' \
		gdb/config.status \
	    &&

	make -j ${BUILD_PARALLELISM} "CFLAGS=-O2 -g -pipe -Wall \
		-Wp,-D_FORTIFY_SOURCE=2 \
		-fexceptions \
		--param=ssp-buffer-size=4 \
		-m64 \
		-mtune=generic \
		${EXTRA_GCC_OPTIONS}" \
		"LDFLAGS=-Wl,-z,relro"

else
	true
fi &&

if $do_install; then

	make install

else
	true
fi
