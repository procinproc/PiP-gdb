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

opt_with_rpm_default="--without-rpm"
opt_with_expat_default="--without-expat"

usage()
{
	echo >&2 "Usage: `basename $0` [-b|-i] --prefix=<DIR> --with-pip=<PIP_DIR>"
	echo >&2 "    [default] : build and install"
	echo >&2 "	-b      : build only, do not install"
	echo >&2 "	-i      : install only, do not build"
	exit 2
}

prefixdir=
pipdir=
program_prefix=

do_build=true
do_install=true
do_clean=true

do_check=false
packages=

while [ x"$1" != x ]; do
    arg=$1
    case $arg in
	-b)	do_install=false; do_clean=false;;
	-k)	do_install=false;;
	-i)	do_build=false;;
	--prefix=*)   installdir=`expr "${arg}" : "--prefix=\(.*\)"`;;
	--with-pip=*) withpip=$arg
	              program_prefix=--program-prefix=pip-
		      ;;
	--with-glibc-libdir=*) true;;
	--missing) do_check=true;;
	--package=*)  packages="${packages} `expr "${arg}" : "--package=\(.*\)"`";;
	*)      usage;;
    esac
    shift
done

if $do_check; then
    if [ x"${packages}" != x ]; then
	echo >&2 "'--missing' and '--packages' cannot be specified at once"
	exit 1
    fi
fi

curdir=`dirname $0`
cppflags=
cflags=

build_flags () {
    installdir=$1
    if [ -d ${installdir}/bin ]; then
	export PATH=${installdir}/bin:${PATH}
    fi
    if [ -d ${installdir}/include ]; then
	if [ x"${cppflags}" == x ]; then
	    cppflags="-I${installdir}/include"
	else
	    cppflags="${cppflags},-I${installdir}/include"
	fi
    fi
    if [ -d ${installdir}/lib ]; then
	ccflags="-L${installdir}/lib ${ccflags}"
	if [ x"${ldflags}" == x ]; then
	    ldflags="-rpath=${installdir}/lib"
	else
	    ldflags="${ldflags},-rpath=${installdir}/lib"
	fi
    fi
}

pkgfail=false;

echo >&2 -n "Checking texinfo .. "
instdir=/usr
flag_installed=false
for pkg in $packages; do
    name=`expr "${pkg}" : "\([^:]*\)"`
    if [ $name == "texinfo" ]; then
	instdir=`expr "${pkg}" : "[^:]*:\(.*\)"`
	flag_installed=true
	break;
    fi
done
if ! [ -x ${instdir}/bin/makeinfo ]; then
    pkgfail=true
    if ! $do_check; then
	echo >&2 "seems not to be installed"
    else
	echo "texinfo https://ftp.gnu.org/gnu/texinfo/texinfo-5.1.tar.gz"
    fi
else
    echo >&2 "OK"
    if $flag_installed; then
	build_flags $instdir
    fi
fi

echo >&2 -n "Checking readline .. "
instdir=/usr
flag_installed=false
for pkg in $packages; do
    name=`expr "${pkg}" : "\([^:]*\)"`
    if [ $name == "readline" ]; then
	instdir=`expr "${pkg}" : "[^:]*:\(.*\)"`
	flag_installed=true
	break;
    fi
done
if ! [ -f ${instdir}/include/readline/readline.h ]; then
    pkgfail=true
    if ! $do_check; then
	echo >&2 "seems not to be installed"
    else
	echo "readline https://ftp.gnu.org/gnu/readline/readline-6.2.tar.gz"
    fi
else
    echo >&2 "OK"
    if $flag_installed; then
	build_flags $instdir
    fi
fi

echo >&2 "Checking other required packages ... "
centos_version=`cut -d ' ' -f 4 /etc/redhat-release`;
case $centos_version in
    7.*) pkgs_needed="gd-devel libpng-devel zlib-devel libselinux-devel audit-libs-devel libcap-devel nss-devel systemtap-sdt-devel libstdc++-static glibc-static ncurses-devel xz-devel rpm-devel expat-devel python-devel texinfo-tex texlive-ec texlive-cm-super dejagnu";;
    8.*) pkgs_needed="gd-devel libpng-devel zlib-devel libselinux-devel audit-libs-devel libcap-devel nss-devel systemtap-sdt-devel ncurses-devel xz-devel rpm-devel expat-devel python36-debug info texlive";;
esac

for pkgn in $pkgs_needed; do
    if ! yum list installed $pkgn >/dev/null 2>&1; then
	pkgfail=true;
	echo >&2 "'$pkgn' package is not installed"
    fi
done

if $do_check; then
    exit 0
elif $pkgfail ; then
    echo >&2 "WARNING: Some packages are missing and installation might be failed"
else
    echo >&2 "All required packages found"
fi

if [ x"${installdir}" == x -o x"${withpip}" == x ]; then
    usage;
fi

pipdir=`expr "${withpip}" : "--with-pip=\(.*\)"`;
if ! [ -x ${pipdir}/lib/libpip.so ]; then
    echo >&2 "${pipdir} seems not to be PiP directory"
fi

case `uname -m` in
aarch64)
	opt_werror=--disable-werror
	opt_with_rpm=${opt_with_rpm_default} #--with-rpm=librpm.so.3
	opt_inprocess_agent=-disable-inprocess-agent
	opt_host_arch=
	host=aarch64-redhat-linux-gnu
	;;
x86_64)
	opt_werror=--enable-werror
	opt_with_rpm=${opt_with_rpm_default} #--with-rpm
	opt_inprocess_agent=-enable-inprocess-agent
	opt_host_arch='-m64 -mtune=generic'
	host=x86_64-redhat-linux-gnu
	;;
esac

: ${BUILD_PARALLELISM=`getconf _NPROCESSORS_ONLN`}
: ${EXTRA_CONFIGURE_OPTIONS="${opt_werror} ${opt_with_rpm} ${opt_inprocess_agent} --enable-targets=s390-linux-gnu,powerpc-linux-gnu,powerpcle-linux-gnu"}
: ${EXTRA_GCC_OPTIONS="-fstack-protector-strong -grecord-gcc-switches"}

if [ x"${cppflags}" != x ]; then
    cppflags="-Wp,${cppflags}"
fi
if [ x"${ldflags}" != x ]; then
    ldflags="-Wl,${ldflags}"
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
		${opt_with_expat_default} \
		--without-libexpat-prefix \
		--enable-tui \
		--without-python \
		--with-lzma \
		--without-libunwind \
		--enable-64-bit-bfd \
		--with-auto-load-dir='$debugdir:$datadir/auto-load' \
		--with-auto-load-safe-path='$debugdir:$datadir/auto-load:/usr/bin/mono-gdb.py' \
		${EXTRA_CONFIGURE_OPTIONS} \
		--prefix=${installdir} ${program_prefix} \
	        ${withpip} \
		${host} \
	    &&

	make -j ${BUILD_PARALLELISM} \
	        "CFLAGS=-O2 -g -pipe -Wall \
		 ${cppflags} ${ccflags} ${ldflags} \
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

	make -j ${BUILD_PARALLELISM} \
	        "CFLAGS=-O2 -g -pipe -Wall \
		 ${cppflags} ${ccflags} ${ldflags} \
		 -Wp,-D_FORTIFY_SOURCE=2 \
		 -fexceptions \
		 --param=ssp-buffer-size=4 \
		 ${opt_host_arch} \
		 ${EXTRA_GCC_OPTIONS}" \
		"LDFLAGS=-Wl,-z,relro"

else
	true
fi

if $do_install; then

	make install

else
	true
fi
