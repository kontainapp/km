#!/bin/bash

set -x

KM_TOP=../$(git rev-parse --show-cdup)

cd cpython
ld -static -nostdlib --gc-sections -T ${KM_TOP}/km.ld -o python.km Programs/python.o libpython3.7m.a -L/usr/lib64 -lz ${KM_TOP}/build/runtime/libruntime.a && chmod a-x python.km
