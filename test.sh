#!/bin/sh

if ! which runtest; then
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
