#!/bin/bash
#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
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

