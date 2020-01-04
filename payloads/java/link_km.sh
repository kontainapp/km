#!/bin/bash
#
# Link java.kmd
#
set -x

KM_TOP=$(git rev-parse --show-cdup) 
${KM_TOP}/tools/kontain-gcc -rdynamic -Wl,--rpath=/opt/kontain/lib64:/lib64:jdk/build/linux-x86_64-server-release/jdk/lib:build/linux-x86_64-server-release/jdk/lib/server \
    -Wl,--hash-style=both -Wl,-z,defs -Wl,-z,noexecstack -Wl,-O1 -m64 -Wl,--allow-shlib-undefined -Wl,--exclude-libs,ALL \
    -Wl,-rpath,\$ORIGIN -Wl,-rpath,\$ORIGIN/../lib -Ljdk/build/linux-x86_64-server-release/support/modules_libs/java.base \
    -o jdk/build/linux-x86_64-server-release/support/native/java.base/java_objs/java.kmd \
    jdk/build/linux-x86_64-server-release/support/native/java.base/java/main.o -ljli -lpthread -ldl