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

case $# in
0)	echo >&2 Usage: $0 "--prefix=<DIR> [--with-pip=<PIP_DIR> --with-glibc-libdir=<GLIBC_LIBDIR>]"
	exit 1
	;;
esac

program_prefix=
case $@ in
*--with-pip*)
	program_prefix=--program-prefix=pip-
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
	--without-python \
	--with-rpm \
	--with-lzma \
	--without-libunwind \
	--enable-64-bit-bfd \
	--enable-inprocess-agent \
	--with-auto-load-dir='$debugdir:$datadir/auto-load' \
	--with-auto-load-safe-path='$debugdir:$datadir/auto-load:/usr/bin/mono-gdb.py' \
	--enable-targets=s390-linux-gnu,powerpc-linux-gnu,powerpcle-linux-gnu \
	"$@" ${program_prefix} \
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
