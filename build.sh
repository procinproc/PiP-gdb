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
	echo >&2 "Usage: `basename $0` [-b [-k]] --prefix=<DIR> --with-pip=<PIP_DIR>"
	echo >&2 "       `basename $0`  -i"
#	echo >&2 "	-B      : build only, do not install, do not clean"
	echo >&2 "	-b      : build only, do not install"
	echo >&2 "	-i      : install only, do not build"
	exit 2
}


echo "Checking required packages ... "

centos_version=`cut -d ' ' -f 4 /etc/redhat-release`;
case $centos_version in
    7.*) pkgs_needed="gd-devel libpng-devel zlib-devel libselinux-devel audit-libs-devel libcap-devel nss-devel systemtap-sdt-devel libstdc++-static glibc-static readline-devel ncurses-devel xz-devel rpm-devel expat-devel python-devel texinfo texinfo-tex texlive-ec texlive-cm-super dejagnu";;
    8.*) pkgs_needed="gd-devel libpng-devel zlib-devel libselinux-devel audit-libs-devel libcap-devel nss-devel systemtap-sdt-devel readline-devel ncurses-devel xz-devel rpm-devel expat-devel python36-debug info texlive";;
esac

pkgfail=false;
nopkg=false;
for pkgn in $pkgs_needed; do
    if ! yum info $pkgn >/dev/null 2>&1; then
	pkgfail=true;
	echo "'$pkgn' package is not installed but required"
    fi
done
if [ $pkgfail == false ]; then
    echo "All required packages found"
fi

do_clean=true
do_build=true
do_install=true

case `uname -m` in
aarch64)
	opt_werror=--disable-werror
	opt_with_rpm=--with-rpm=librpm.so.3
	opt_inprocess_agent=-disable-inprocess-agent
	opt_host_arch=
	host=aarch64-redhat-linux-gnu
	;;
x86_64)
	opt_werror=--enable-werror
	opt_with_rpm=--with-rpm
	opt_inprocess_agent=-enable-inprocess-agent
	opt_host_arch='-m64 -mtune=generic'
	host=x86_64-redhat-linux-gnu
	;;
esac

: ${BUILD_PARALLELISM=`getconf _NPROCESSORS_ONLN`}
: ${EXTRA_CONFIGURE_OPTIONS="${opt_werror} ${opt_with_rpm} ${opt_inprocess_agent} --enable-targets=s390-linux-gnu,powerpc-linux-gnu,powerpcle-linux-gnu"}
: ${EXTRA_GCC_OPTIONS="-fstack-protector-strong -grecord-gcc-switches"}

program_prefix=
case $@ in
*--with-pip*)
	program_prefix=--program-prefix=pip-
	;;
esac
if [ x$program_prefix == x ]; then
    usage;
fi

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

if [ $pkgfail == true ]; then
    exit 1;
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
		--with-auto-load-dir='$debugdir:$datadir/auto-load' \
		--with-auto-load-safe-path='$debugdir:$datadir/auto-load:/usr/bin/mono-gdb.py' \
		${EXTRA_CONFIGURE_OPTIONS} \
		"$@" ${program_prefix} \
		${host} \
	    &&

	make -j ${BUILD_PARALLELISM} "CFLAGS=-O2 -g -pipe -Wall \
		-fexceptions \
		--param=ssp-buffer-size=4 \
		${opt_host_arch} \
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
		${opt_host_arch} \
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
