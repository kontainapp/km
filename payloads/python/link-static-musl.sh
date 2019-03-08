#!/bin/bash

set -x

MUSL_TOP=../$(git rev-parse --show-cdup)runtime/musl

cd cpython
gcc -static -nostdlib ${MUSL_TOP}/lib/crt1.o -o python Programs/python.o libpython3.7m.a ${MUSL_TOP}/lib/libc.a
