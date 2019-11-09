#!/bin/bash

set -x

KM_TOP=../$(git rev-parse --show-cdup)
PATH=${KM_TOP}/tools:$PATH

MODULE=${1:-numpy}
LINK_FILE=/home/msterin/workspace/km/payloads/python/cpython/Modules/${MODULE}/linkline_km.txt

cd cpython
kontain-gcc -ggdb Programs/python.o \
         @${LINK_FILE} -L/home/msterin/workspace/km/payloads/python/cpython/Modules/${MODULE}/build/temp.linux-x86_64-3.7 -lnpymath -lnpysort \
         libpython3*.a -lz -lssl -lcrypto $LDLIBS -o python.km && chmod a-x python.km

