#!/bin/bash
#
# Links Python.km from python libs and km 'dlstatic' builditns mentioned in $target/linkline_km.txt
#
set -e
[ "$TRACE" ] && set -x
if [ "$1" == "--help" ] ; then
  cat << EOF
Link cpython.km:

link.km location target extras

location - where cpython artifacts are. Default: $(dirname ${BASH_SOURCE[0]})/cpython
target   - where to put the results.    Default: $(dirname ${BASH_SOURCE[0]})/cpython
extra    - optional list of files to add (e.g. @f1 @f2...). Default: none
name     - optional km name. Defaul: python.km
EOF
   exit 0
fi

EXTRA_FILES="$3"
NAME=${4:-python.km}
KM_TOP=$(git rev-parse --show-cdup)
PATH=$(realpath ${KM_TOP}/tools):$PATH
BUILD=$(realpath ${1:-cpython})
OUT=$(realpath ${2:-cpython})

cd $OUT
kontain-gcc -ggdb ${BUILD}/Programs/python.o \
   @linkline_km.txt ${EXTRA_FILES} \
   ${BUILD}/libpython3*.a -lz -lssl -lcrypto $LDLIBS \
   -o ${NAME} && chmod a-x ${NAME} && echo Linked: ${OUT}/${NAME}

