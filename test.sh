#!/bin/sh

cd gdb/testsuite &&
make site.exp &&
cat site.exp &&
runtest TRANSCRIPT=y gdb.pip/threads.exp gdb.pip/pip.exp
exit_code=$?

cat gdb.sum

exit $exit_code
