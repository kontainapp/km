#!/bin/bash

if [ -v BASH_TRACING ] ; then set -x ; fi

MUSL_TOP=../$(git rev-parse --show-cdup)runtime/musl

cd cpython
gcc -static -Wl,--gc-sections -nostdlib ${MUSL_TOP}/lib/crt1.o -o python Programs/python.o libpython3.7m.a -lz ${MUSL_TOP}/lib/libc.a
