# How to build this RPM:
#
#	$ rpm -Uvh gdb-8.2-12.el8.src.rpm
#	$ cd .../$SOMEWHERE/.../PiP-gdb
#	$ git diff origin/centos/gdb-8.2-12.el8.branch centos/gdb-8.2-12.el8.pip.branch >~/rpmbuild/SOURCES/gdb-el8-%{pip_gdb_release}.patch
#	$ rpmbuild --define 'pip_major_version 2' -bb pip-gdb.spec
#

%define	pip_gdb_release		pip3
%if %{undefined pip_major_version}
%define pip_major_version	2
%endif
%define pip_prefix		/opt/process-in-process/pip-%{pip_major_version}
%define scl_prefix		pip-

# rpmbuild parameters:
# --with testsuite: Run the testsuite (biarch if possible).  Default is without.
# --with buildisa: Use %%{?_isa} for BuildRequires
# --with asan: gcc -fsanitize=address
# --without python: No python support.
# --with profile: gcc -fprofile-generate / -fprofile-use: Before better
#                 workload gets run it decreases the general performance now.
# --define 'scl somepkgname': Independent packages by scl-utils-build.

%{?scl:%scl_package gdb}
%{!?scl:
#%global pkg_name %{name}
 %global pkg_name gdb
 %global _root_prefix %{_prefix}
 %global _root_datadir %{_datadir}
 %global _root_libdir %{_libdir}
}

Name: %{?scl_prefix}gdb

# Freeze it when GDB gets branched
%global snapsrc    20180828
# See timestamp of source gnulib installed into gdb/gnulib/ .
%global snapgnulib 20161115
%global tarname gdb-%{version}
Version: 8.2

# The release always contains a leading reserved number, start it at 1.
# `upstream' is not a part of `name' to stay fully rpm dependencies compatible for the testing.
Release: 12%{?dist}

License: GPLv3+ and GPLv3+ with exceptions and GPLv2+ and GPLv2+ with exceptions and GPL+ and LGPLv2+ and LGPLv3+ and BSD and Public Domain and GFDL
Group: Development/Debuggers
# Do not provide URL for snapshots as the file lasts there only for 2 days.
# ftp://sourceware.org/pub/gdb/releases/FIXME{tarname}.tar.xz
#Source: %{tarname}.tar.xz
Source: ftp://sourceware.org/pub/gdb/releases/%{tarname}.tar.xz
URL: http://gnu.org/software/gdb/

# For our convenience
%global gdb_src %{tarname}
%global gdb_build build-%{_target_platform}

# error: Installed (but unpackaged) file(s) found: /usr/lib/debug/usr/bin/gdb-gdb.py
# https://lists.fedoraproject.org/archives/list/devel@lists.fedoraproject.org/message/PBOJDOFMWTRV3ZOKNV5HN7IBX5EPHDHF/
%undefine _debuginfo_subpackages

# For DTS RHEL<=7 GDB it is better to use none than a Requires dependency.
%if 0%{!?rhel:1} || 0%{?rhel} > 7
Recommends: %{?scl_prefix}gcc-gdb-plugin%{?_isa}
Recommends: dnf-command(debuginfo-install)
%endif

%if 0%{!?scl:1}
# when manpages were moved from -headless to main
# https://bugzilla.redhat.com/show_bug.cgi?id=1402554
# theoretically should not be required due to versioned dependeny
# below, but it cannot hurt either -- rdieter
Conflicts: gdb-headless < 7.12-29

Summary: A stub package for GNU source-level debugger
#Requires: gdb-headless%{?_isa} = %{version}-%{release}
Requires: gdb-headless%{?_isa}

%description
'gdb' package is only a stub to install gcc-gdb-plugin for 'compile' commands.
See package 'gdb-headless'.

%package headless
Group: Development/Debuggers
%endif

Summary: A GNU source-level debugger for C, C++, Fortran, Go and other languages for PiP (Process in Process)
Requires: gdb

# Make sure we get rid of the old package gdb64, now that we have unified
# support for 32-64 bits in one single 64-bit gdb.
%ifarch ppc64
Obsoletes: gdb64 < 5.3.91
%endif

%ifarch %{arm}
%global have_inproctrace 0
%else
%global have_inproctrace 1
%endif

# gdb-add-index cannot be run even for SCL package on RHEL<=6.
%if 0%{!?rhel:1} || 0%{?rhel} > 6
# eu-strip: -g recognizes .gdb_index as a debugging section. (#631997)
Conflicts: elfutils < 0.149
%endif

# https://fedorahosted.org/fpc/ticket/43 https://fedorahosted.org/fpc/ticket/109
Provides: bundled(libiberty) = %{snapsrc}
Provides: bundled(gnulib) = %{snapgnulib}
Provides: bundled(binutils) = %{snapsrc}
# https://fedorahosted.org/fpc/ticket/130
Provides: bundled(md5-gcc) = %{snapsrc}

# https://fedoraproject.org/wiki/Packaging:Guidelines#BuildRequires_and_.25.7B_isa.7D
%if 0%{?_with_buildisa:1} || 0%{?_with_testsuite:1}
%global buildisa %{?_isa}
%else
%global buildisa %{nil}
%endif

%if 0%{!?rhel:1} || 0%{?rhel} > 7
# https://bugzilla.redhat.com/show_bug.cgi?id=1209492
Recommends: default-yama-scope
%endif

%if 0%{?el6:1}
%global librpmver 1
%else
# FIXME: %elif does not work.
%if 0%{?el7:1}
%global librpmver 3
%else
%if 0%{?fedora} >= 27 || 0%{?rhel} > 7
%global librpmver 8
%else
%global librpmver 7
%endif
%endif
%endif
%if 0%{?__isa_bits} == 64
%global librpmname librpm.so.%{librpmver}()(64bit)
%else
%global librpmname librpm.so.%{librpmver}
%endif
BuildRequires: rpm-libs%{buildisa}
%if 0%{?_with_buildisa:1}
BuildRequires: %{librpmname}
%endif
%if 0%{!?rhel:1} || 0%{?rhel} > 7
Recommends: %{librpmname}
%endif

%if 0%{?el6:1}
# GDB C++11 requires devtoolset gcc.
BuildRequires: %{?scl_prefix}gcc-c++
%endif

# GDB patches have the format `gdb-<version>-bz<red-hat-bz-#>-<desc>.patch'.
# They should be created using patch level 1: diff -up ./gdb (or gdb-6.3/gdb).

#=
#push=Should be pushed upstream.
#fedora=Should stay as a Fedora patch.
#fedoratest=Keep it in Fedora only as a regression test safety.

# Cleanup any leftover testsuite processes as it may stuck mock(1) builds.
#=push+jan
Source2: gdb-orphanripper.c

# Man page for gstack(1).
#=push+jan
Source3: gdb-gstack.man

# /etc/gdbinit (from Debian but with Fedora compliant location).
#=fedora
Source4: gdbinit

# libstdc++ pretty printers from GCC SVN.
%global libstdcxxpython gdb-libstdc++-v3-python-8.1.1-20180626
#=fedora
Source5: %{libstdcxxpython}.tar.xz

# Provide gdbtui for RHEL-5 and RHEL-6 as it is removed upstream (BZ 797664).
#=fedora
Source6: gdbtui

# libipt: Intel Processor Trace Decoder Library
%global libipt_version 2.0
#=fedora
Source7: v%{libipt_version}.tar.gz
#=fedora
Patch1142: v1.5-libipt-static.patch

## [testsuite] Fix false selftest.exp FAIL from system readline-6.3+ (Patrick Palka).
##=fedoratest
#Patch1075: gdb-testsuite-readline63-sigint.patch
##=fedoratest
Patch1119: gdb-testsuite-readline63-sigint-revert.patch

# Include the auto-generated file containing the "Patch:" directives.
# See README.local-patches for more details.
Source8: _gdb.spec.Patch.include
Source9: _gdb.spec.patch.include
%include %{SOURCE8}

Patch9999: gdb-el8-%{pip_gdb_release}.patch

%if 0%{!?rhel:1} || 0%{?rhel} > 6
# RL_STATE_FEDORA_GDB would not be found for:
# Patch642: gdb-readline62-ask-more-rh.patch
# --with-system-readline
BuildRequires: readline-devel%{buildisa} >= 6.2-4
%endif # 0%{!?rhel:1} || 0%{?rhel} > 6

BuildRequires: gcc-c++ ncurses-devel%{buildisa} texinfo gettext flex bison
BuildRequires: expat-devel%{buildisa}
%if 0%{!?rhel:1} || 0%{?rhel} > 6
BuildRequires: xz-devel%{buildisa}
%endif
# dlopen() no longer makes rpm-libsFIXME{?_isa} (it's .so) a mandatory dependency.
BuildRequires: rpm-devel%{buildisa}
BuildRequires: zlib-devel%{buildisa} libselinux-devel%{buildisa}
%if 0%{!?_without_python:1}
%if 0%{?rhel:1} && 0%{?rhel} <= 7
BuildRequires: python-devel%{buildisa}
%else
%global __python %{__python3}
BuildRequires: python3-devel%{buildisa}
%endif
%if 0%{?rhel:1} && 0%{?rhel} <= 7
# Temporarily before python files get moved to libstdc++.rpm
# libstdc++%{bits_other} is not present in Koji, the .spec script generating
# gdb/python/libstdcxx/ also does not depend on the %{bits_other} files.
BuildRequires: libstdc++%{buildisa}
%endif # 0%{?rhel:1} && 0%{?rhel} <= 7
%endif # 0%{!?_without_python:1}
# gdb-doc in PDF, see: https://bugzilla.redhat.com/show_bug.cgi?id=919891#c10
BuildRequires: texinfo-tex
%if 0%{!?rhel:1} || 0%{?rhel} > 6
BuildRequires: texlive-collection-latexrecommended
%endif
# Permit rebuilding *.[0-9] files even if they are distributed in gdb-*.tar:
BuildRequires: /usr/bin/pod2man
%if 0%{!?rhel:1} || 0%{?rhel} > 7
BuildRequires: libbabeltrace-devel%{buildisa}
BuildRequires: guile-devel%{buildisa}
%endif
%global have_libipt 0
%if 0%{!?rhel:1} || 0%{?rhel} > 7 || (0%{?rhel} == 7 && 0%{?scl:1})
%ifarch %{ix86} x86_64
%global have_libipt 1
%if 0%{?el7:1} && 0%{?scl:1}
BuildRequires: cmake
%else
## patch for PiP-gdb
## it seems libipt-devel is not publicly distributed, and not mandatory
# BuildRequires: libipt-devel%{buildisa}
%endif
%endif
%endif
%if 0%{!?rhel:1} || 0%{?rhel} > 6
# See https://bugzilla.redhat.com/show_bug.cgi?id=1593280
# DTS RHEL-6 has mpfr-2 while GDB requires mpfr-3.
BuildRequires: mpfr-devel%{buildisa}
%endif

%if 0%{?_with_testsuite:1}

# Ensure the devel libraries are installed for both multilib arches.
%global bits_local %{?_isa}
%global bits_other %{?_isa}
%ifarch s390x
%if 0%{!?rhel:1} || 0%{?rhel} < 8
%global bits_other (%{__isa_name}-32)
%endif
%else #!s390x
%ifarch ppc
%global bits_other (%{__isa_name}-64)
%else #!ppc
%ifarch sparc64 ppc64 s390x x86_64
%global bits_other (%{__isa_name}-32)
%endif #sparc64 ppc64 s390x x86_64
%endif #!ppc
%endif #!s390x

BuildRequires: sharutils dejagnu
# gcc-objc++ is not covered by the GDB testsuite.
BuildRequires: gcc gcc-c++ gcc-gfortran
%if 0%{!?rhel:1} || 0%{?rhel} < 8
BuildRequires: gcc-objc
%endif
%if 0%{!?rhel:1} || 0%{?rhel} > 7
BuildRequires: gcc-gdb-plugin%{?_isa}
%endif
%if 0%{?rhel:1} && 0%{?rhel} < 7
BuildRequires: gcc-java libgcj%{bits_local} libgcj%{bits_other}
# for gcc-java linkage:
BuildRequires: zlib-devel%{bits_local} zlib-devel%{bits_other}
%endif
# Exception for RHEL<=7
%ifarch aarch64
%if 0%{!?rhel:1} || 0%{?rhel} == 7
BuildRequires: gcc-go
BuildRequires: libgo-devel%{bits_local} libgo-devel%{bits_other}
%endif
%else
%if 0%{!?rhel:1} || 0%{?rhel} == 7
BuildRequires: gcc-go
BuildRequires: libgo-devel%{bits_local} libgo-devel%{bits_other}
%endif
%endif
# archer-sergiodj-stap-patch-split
BuildRequires: systemtap-sdt-devel
%if 0%{?rhel:1} && 0%{?rhel} <= 7
# Copied from prelink-0.4.2-3.fc13.
# Prelink is not yet ported to ppc64le.
%ifarch %{ix86} alpha sparc sparcv9 sparc64 s390 s390x x86_64 ppc ppc64
# Prelink is broken on sparcv9/sparc64.
%ifnarch sparc sparcv9 sparc64
BuildRequires: prelink
%endif
%endif
%endif
# MPX not supported on RHEL
%if 0%{!?rhel:1}
%ifarch %{ix86} x86_64
BuildRequires: libmpx%{bits_local} libmpx%{bits_other}
%endif
BuildRequires: opencl-headers ocl-icd-devel%{bits_local} ocl-icd-devel%{bits_other}
%endif
%if 0%{!?rhel:1}
# Fedora arm+ppc64le do not yet have fpc built.
%ifnarch %{arm} ppc64le
BuildRequires: fpc
%endif
%endif
# Copied from: gcc-6.2.1-1.fc26
# Exception for RHEL<=7
%ifarch s390x
%if 0%{!?rhel:1} || 0%{?rhel} <= 7
BuildRequires: gcc-gnat
BuildRequires: libgnat%{bits_local} libgnat%{bits_other}
%endif
%else
%ifarch %{ix86} x86_64 ia64 ppc %{power64} alpha s390x %{arm} aarch64
%if 0%{!?rhel:1} || 0%{?rhel} <= 7
BuildRequires: gcc-gnat
BuildRequires: libgnat%{bits_local} libgnat%{bits_other}
%endif
%endif
%endif
BuildRequires: glibc-devel%{bits_local} glibc-devel%{bits_other}
BuildRequires: libgcc%{bits_local} libgcc%{bits_other}
BuildRequires: libgfortran%{bits_local} libgfortran%{bits_other}
# libstdc++-devel of matching bits is required only for g++ -static.
BuildRequires: libstdc++%{bits_local} libstdc++%{bits_other}
%if 0%{!?rhel:1} || 0%{?rhel} > 6
%ifarch %{ix86} x86_64
BuildRequires: libquadmath%{bits_local} libquadmath%{bits_other}
%endif
%endif
BuildRequires: glibc-static%{bits_local}
# multilib glibc-static is open Bug 488472:
#BuildRequires: glibc-static%{bits_other}
# Exception for RHEL<=7
%ifarch s390x
BuildRequires: valgrind%{bits_local}
%if 0%{!?rhel:1} || 0%{?rhel} > 7
BuildRequires: valgrind%{bits_local} valgrind%{bits_other}
%endif
%else
BuildRequires: valgrind%{bits_local} valgrind%{bits_other}
%endif
%if 0%{!?rhel:1} || 0%{?rhel} > 6
BuildRequires: xz
%endif
%if 0%{!?rhel:1} || 0%{?rhel} > 7
BuildRequires: rust
%endif

%endif # 0%{?_with_testsuite:1}

%{?scl:Requires:%scl_runtime}

# FIXME: The text needs to be duplicated to prevent 2 empty heading lines.
%if 0%{!?scl:1}
%description headless
GDB, the GNU debugger, allows you to debug programs written in C, C++,
Java, and other languages, by executing them in a controlled fashion
and printing their data.
%else
%description
GDB, the GNU debugger, allows you to debug programs written in C, C++,
Java, and other languages, by executing them in a controlled fashion
and printing their data.
%endif

%package gdbserver
Summary: A standalone server for GDB (the GNU source-level debugger)
Group: Development/Debuggers

%description gdbserver
GDB, the GNU debugger, allows you to debug programs written in C, C++,
Java, and other languages, by executing them in a controlled fashion
and printing their data.

This package provides a program that allows you to run GDB on a different
machine than the one which is running the program being debugged.

%package doc
Summary: Documentation for GDB (the GNU source-level debugger)
License: GFDL
Group: Documentation
BuildArch: noarch
%if 0%{?scl:1}
# As of F-28, packages won't need to call /sbin/install-info by hand
# anymore.  We make an exception for DTS here.
# https://lists.fedoraproject.org/archives/list/devel@lists.fedoraproject.org/thread/MP2QVJZBOJZEOQO2G7UB2HLXKXYPF2G5/
Requires(post): /sbin/install-info
Requires(preun): /sbin/install-info
%endif

%description doc
GDB, the GNU debugger, allows you to debug programs written in C, C++,
Java, and other languages, by executing them in a controlled fashion
and printing their data.

This package provides INFO, HTML and PDF user manual for GDB.

%prep
%setup -q -n %{gdb_src}

%if 0%{?rhel:1} && 0%{?rhel} <= 7
# libstdc++ pretty printers.
tar xJf %{SOURCE5}
%endif # 0%{?rhel:1} && 0%{?rhel} <= 7

%if 0%{have_libipt} && 0%{?el7:1} && 0%{?scl:1}
tar xzf %{SOURCE7}
(
 cd processor-trace-%{libipt_version}
%patch1142 -p1
)
%endif

# Files have `# <number> <file>' statements breaking VPATH / find-debuginfo.sh .
(cd gdb;rm -fv $(perl -pe 's/\\\n/ /' <Makefile.in|sed -n 's/^YYFILES = //p'))

# *.info* is needlessly split in the distro tar; also it would not get used as
# we build in %{gdb_build}, just to be sure.
find -name "*.info*"|xargs rm -f

# Apply patches defined on _gdb.spec.Patch.include

# Include the auto-generated "%patch" directives.
# See README.local-patches for more details.
%include %{SOURCE9}

%if 0%{!?el6:1}
for i in \
  gdb/python/lib/gdb/FrameWrapper.py \
  gdb/python/lib/gdb/backtrace.py \
  gdb/python/lib/gdb/command/backtrace.py \
  ;do
  test -e $i
  : >$i
done
%endif

%if 0%{?rhel:1} && 0%{?rhel} <= 7
%patch1119 -p1
%endif

%patch9999 -p1

find -name "*.orig" | xargs rm -f
! find -name "*.rej" # Should not happen.

# RL_STATE_FEDORA_GDB would not be found for:
# Patch642: gdb-readline62-ask-more-rh.patch
# --with-system-readline
rm -rf readline/*

%build
rm -rf %{buildroot}

# use _prefix=/usr by default to share configuration files with original gdb
DESTDIR="$RPM_BUILD_ROOT" sh ./build.sh -b \
	--prefix=%{_prefix} \
	--with-pip=%{pip_prefix}

%install

mkdir -p "$RPM_BUILD_ROOT"/%{_bindir}
cp gdb/gdb "$RPM_BUILD_ROOT"/%{_bindir}/pip-gdb

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf "$RPM_BUILD_ROOT"

%files
%defattr(-,root,root)
%{_bindir}/pip-gdb
