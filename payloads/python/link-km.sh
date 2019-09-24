#!/bin/bash

set -x

KM_TOP=../$(git rev-parse --show-cdup)
PATH=${KM_TOP}/tools:$PATH

cd cpython
kontain-gcc -ggdb Programs/python.o libpython3*.a -Wl,-u__km_dllist -Wl,-u__km_ndllist /tmp/Bar/lib_bundle.o -lz -o python.km && chmod a-x python.km

