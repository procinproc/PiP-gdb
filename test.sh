#!/bin/sh

if [ x"$CC" = x ]; then
    CC=cc
fi
if ! $CC check_ptrace.c -o check_ptrace > /dev/null 2>&1 || 
    ! ./check_ptrace; then
    echo "Failed to call ptrace() and PiP-gdb test is skipped"
    rm -f check_ptrace
    exit 0
fi
rm -f check_ptrace

if ! which runtest >/dev/null 2>&1; then
    echo >&2 "DejaGnu seems not installed and PiP-gdb test is skipped"
    exit 0
fi

cd gdb/testsuite &&
make site.exp &&
cat site.exp &&
runtest TRANSCRIPT=y gdb.pip/threads.exp gdb.pip/pip.exp
exit_code=$?

if test -f gdb.sum; then
  DEJAGNU_TEST_CLASS=PiP-gdb ./dejagnu-to-xml.awk gdb.sum >gdb.pip.xml &&
  if fgrep '<testcase ' gdb.pip.xml >/dev/null; then
    if egrep '<(failure|error) ' gdb.pip.xml >/dev/null; then
      exit_code=1
    fi
  fi
fi

cat gdb.sum

exit $exit_code
