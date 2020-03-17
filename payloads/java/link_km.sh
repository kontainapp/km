#!/bin/bash
#
# Link java.kmd
#
#set -x

KM_TOP=$(git rev-parse --show-toplevel)
if [[ -z "$1" ]] ; then
   echo Usage: link_km.sh jdk_dir
   exit 1
fi

JDK_DIR=$1
OUT_DIR=${JDK_DIR}/images/jdk/bin

mkdir -p ${OUT_DIR}
${KM_TOP}/tools/kontain-gcc -rdynamic \
    -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack \
    -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL \
    ${JDK_DIR}/support/native/java.base/java/main.o \
    -L${JDK_DIR}/images/jdk/lib/jli -ljli \
    -o ${OUT_DIR}/java.kmd

