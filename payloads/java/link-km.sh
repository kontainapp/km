#!/bin/bash
#
# Link java.kmd
#
set -e ; [ "$TRACE" ] && set -x

KM_TOP=$(git rev-parse --show-toplevel)
if [[ -z "$1" ]] ; then
   echo Usage: link-km.sh jdk_dir
   exit 1
fi

JDK_DIR=$1
OUT_DIR=${JDK_DIR}/images/jdk/bin

mkdir -p ${OUT_DIR}
${KM_TOP}/tools/bin/kontain-gcc -dynamic -rdynamic \
    -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack \
    -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL \
    ${JDK_DIR}/support/native/java.base/java/main.o \
    -L${JDK_DIR}/images/jdk/lib/jli -ljli \
    -L /opt/kontain/lib -lmimalloc \
    -o ${OUT_DIR}/java.kmd

