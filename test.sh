#!/bin/sh

cd gdb/testsuite

make site.exp

runtest gdb.pip/threads.exp gdb.pip/pip.exp

exst=$?
cat gdb.sum

exit $?
