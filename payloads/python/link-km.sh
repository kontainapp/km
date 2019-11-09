#!/bin/bash

set -x

KM_TOP=../$(git rev-parse --show-cdup)
PATH=${KM_TOP}/tools:$PATH

MODULE=markupsafe
LINK_FILE=/home/msterin/workspace/km/payloads/python/cpython/Modules/${MODULE}/linkline_km.txt

cd cpython
kontain-gcc -ggdb Programs/python.o libpython3*.a @${LINK_FILE} -lz -lssl -lcrypto $LDLIBS -o python.km && chmod a-x python.km

