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
#
# Links Python.km from python libs and km 'dlstatic' builditns mentioned in $target/linkline_km.txt
#
set -e
[ "$TRACE" ] && set -x

if [ "$1" == "--help" ]; then
   cat <<EOF
Link cpython.km:

link.km location target extras

location - where cpython artifacts are. Default: $(dirname ${BASH_SOURCE[0]})/cpython
target   - where to put the results.    Default: $(dirname ${BASH_SOURCE[0]})/cpython
extra    - optional list of files to add (e.g. @f1 @f2...). Default: none
name     - optional km name. Defaul: python.km
EOF
   exit 0
fi

BUILD=$(realpath ${1:-cpython})
OUT=$(realpath ${2:-cpython})
EXTRA_FILES="$3"
NAME=${4:-python.km}

KM_TOP=$(git rev-parse --show-toplevel)
PATH=$(realpath ${KM_TOP}/tools/bin):$PATH

KM_OPT_BIN=/opt/kontain/bin

cd $OUT
echo kontain-gcc -pthread -ggdb ${BUILD}/Programs/python.o \
   @linkline_km.txt ${EXTRA_FILES} ${OUT}/python.km.symbols.o \
   ${BUILD}/libpython3*.a -lz -lssl -lcrypto -lsqlite3 $LDLIBS \
   -o ${NAME}
kontain-gcc -pthread -ggdb ${BUILD}/Programs/python.o \
   @linkline_km.txt ${EXTRA_FILES} ${OUT}/python.km.symbols.o \
   ${BUILD}/libpython3*.a -lz -lssl -lcrypto -lsqlite3 $LDLIBS \
   -o ${NAME} && chmod a-x ${NAME} && echo Linked: ${OUT}/${NAME}

kontain-gcc -dynamic -Xlinker -export-dynamic -pthread -ggdb ${BUILD}/Programs/python.o \
   -Xlinker --undefined=strtoull_l \
   ${BUILD}/libpython3*.a -lsqlite3 $LDLIBS \
   -o ${NAME}d

kontain-gcc -dynamic -Xlinker -export-dynamic -pthread -ggdb ${BUILD}/Programs/python.o \
   -Xlinker --undefined=strtoull_l \
   ${BUILD}/libpython3*.a -lsqlite3 $LDLIBS \
   -L/opt/kontain/lib -lmimalloc \
   -o ${NAME}d.mimalloc

# Add python->km symlink and make python to looking for libs in correct place
# We want it to work in containers and dev boxes, so locking to /opt/kontain and
# making KM_BLDDIR placement optional
ln -sf ${KM_OPT_BIN}/km python
cat >${KM_OPT_BIN}/pyvenv.cfg <<-EOF
home = ${OUT}
include-system-site-packages = false
version = ${PYTHON_VERSION}
runtime = kontain
runtime_branch = ${SRC_BRANCH}
runtime_sha = ${SRC+SHA}
EOF
if [ -d "${KM_BLDDIR}" ]; then
   cp ${KM_OPT_BIN}/pyvenv.cfg ${KM_BLDDIR}
fi
