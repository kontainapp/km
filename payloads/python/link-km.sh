#!/bin/bash

set -x

KM_TOP=../$(git rev-parse --show-cdup)

cd cpython
cc -static -no-pie -ggdb -pthread Programs/python.o libpython3*.a -o python.km -L/usr/lib64 -lz  -specs=${KM_TOP}/gcc-km.spec -L ${KM_TOP}/build/runtime && chmod a-x python.km

