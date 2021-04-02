#!/bin/sh

set -x

yum-config-manager --enable PowerTools
yum -y install \
	dejagnu \
	expat-devel \
	glib2-devel \
	guile-devel \
	libselinux-devel \
	libuuid-devel \
	mpfr-devel \
	ncurses-devel \
	readline-devel \
	rpm-devel \
	texinfo \
	texinfo-tex \
	xz-devel \
	zlib-devel

cd /host &&
time ./build.sh --prefix=$HOME/pip \
	--with-pip=/opt/process-in-process/pip-$PIP_VERSION &&
time ./test.sh

