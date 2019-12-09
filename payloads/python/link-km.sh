#!/bin/bash

if [ -v BASH_TRACING ] ; then set -x ; fi

KM_TOP=$(git rev-parse --show-cdup)
PATH=${KM_TOP}/tools:$PATH

BUILD=${1:-cpython}
OUT=${2:-cpython}

kontain-gcc -ggdb ${BUILD}/Programs/python.o ${BUILD}/libpython3*.a -lz -lssl -lcrypto -o ${OUT}/python.km && chmod a-x ${OUT}/python.km

