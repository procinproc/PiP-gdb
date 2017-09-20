#!/bin/sh

if [ -z $1 ]; then
    echo $0 "<PIP_INSTALL_DIR> [ <LDLINUX_PATH> [ <GDB_INSTALL_DIR> ] ]"
    exit 1
fi

PIP_PREFIX=$1

if [ -n $2 ]; then
    if [ -x $2 ];
    then
	LDLINUX=-Wl,--dynamic_linker=$2
    else
	echo '$2' is not LD-LINUX or unable to find
	exit 1
    fi
fi

if [ -n $3 ]; then
    PREFIX="--prefix=$3"
fi

make clean
make distclean
find . -name config.cache -delete

./configure --enable-gdb-build-warnings=,-Wno-unused --enable-werror --with-separate-debug-dir=/usr/lib/debug --disable-sim --disable-rpath --with-system-readline --with-expat --without-libexpat-prefix --enable-tui --with-python --with-rpm --with-lzma --without-libunwind --enable-64-bit-bfd --enable-inprocess-agent '--with-auto-load-dir=$debugdir:$datadir/auto-load' '--with-auto-load-safe-path=$debugdir:$datadir/auto-load:/usr/bin/mono-gdb.py' --enable-targets=s390-linux-gnu,powerpc-linux-gnu,powerpcle-linux-gnu x86_64-redhat-linux-gnu ${PREFIX}

 make -j8 'CFLAGS=-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -DENABLE_PIP' "LDFLAGS=-Wl,-z,relro ${LDLINUX} -Wl,-rpath=${PIP_PREFIX}/lib" maybe-configure-gdb

perl -i.relocatable -pe 's/^(D\[".*_RELOCATABLE"\]=" )1(")$/${1}0$2/' gdb/config.status

make -j8 'CFLAGS=-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -DENABLE_PIP' "LDFLAGS=-Wl,-z,relro -Wl,--dynamic-linker=${PIP_PREFIX}/lib/ld-2.17.so -Wl,-rpath=${PIP_PREFIX}/lib"

if [ -n ${PREFIX} ]; then
make install
fi
