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

# Use this script to create a rootfs for the python runenv image. We try to use
# as close to default as possible to avoid messing with PYTHONHOME and
# PYTHONPATH. The layout would be the follow. Using python:3.X-alpine as
# reference.
# /opt/kontain/bin/km (mapped into container through volume)
# /usr/local/bin/python, python{X}, python{X.Y}
# /usr/local/lib64 -> /usr/local/lib - to comply with Fedora layout
# /usr/local/lib/python{X,Y} (cpython/Lib)
# /usr/local/lib/python{X,Y}/lib-dynload (cpython/build/lib.linux-x86_64-${X,Y})
# arg#4 is oython executable to use
# arg#5 set to "dynamic" makes us use real .so files instead of thumbstones

set -e ; [ "$TRACE" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CURRENT=$(readlink -m $(dirname $0))

function usage() {
    cat <<- EOF
usage: $PROGNAME <PYTHON SRC> <RUNENV PATH> <PYTHON VERSION> <PYTHON.KM> [ "dynamic" ]

Script used to create a rootfs for python runenv image.

EOF
}

if [[ $# != 4 && $# != 5 ]]; then
    usage
    exit 1
fi

readonly PYTHON_SRC=$(readlink -m $1)
readonly RUNENV_PATH=$(readlink -m $2)
readonly PYTHON_VERSION=$3
readonly PYTHON_VERSION_MAJ=${3%%.*}
readonly PYTHON_KM=${PYTHON_SRC}/${4:-python.km}
readonly DYNAMIC=${5:-static}

readonly RUNENV_PYTHON_BIN=/usr/local/bin
readonly RUNENV_PYTHON_LIB=/usr/local/lib/python${PYTHON_VERSION}
readonly RUNENV_PYTHON_LIB_DYNLOAD=${RUNENV_PYTHON_LIB}/lib-dynload

readonly PYTHON_SRC_LIB=${PYTHON_SRC}/build/lib.linux-x86_64-${PYTHON_VERSION}
readonly PYTHON_SRC_LIB_STUBS=$(find ${PYTHON_SRC_LIB} -name '*.so')
readonly RUNENV_PYTHON_LIB_STUBS=$(echo ${PYTHON_SRC_LIB_STUBS} | sed "s|${PYTHON_SRC_LIB}|${RUNENV_PYTHON_LIB_DYNLOAD}|g")

readonly RUNENV_EXCLUDE='--exclude=*/test --exclude=*/__pycache__ --exclude *.exe --exclude *.whl'

function main() {

    echo "Creating rootfs from ${PYTHON_SRC} inside ${RUNENV_PATH} for python${PYTHON_VERSION}..."

    rm -rf ${RUNENV_PATH} && mkdir -p ${RUNENV_PATH}
    mkdir -p $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_BIN})
    mkdir -p $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_LIB_DYNLOAD})

    echo "Installing symlinks to ${RUNENV_PATH} ..."
    # Install python.km into /usr/local/bin
    install -s ${PYTHON_KM} $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_BIN}/python${PYTHON_VERSION})
    # Create the symlinks
    ln -s python${PYTHON_VERSION} $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_BIN}/python${PYTHON_VERSION_MAJ})
    ln -s python${PYTHON_VERSION_MAJ} $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_BIN}/python)

    echo "Installing module libs..."
    # Install module libs to /usr/local/lib/python{X,Y}
    tar -cf - ${RUNENV_EXCLUDE} -C ${PYTHON_SRC}/Lib . | tar -C $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_LIB}) -xf -
    ln -s lib $(realpath -m ${RUNENV_PATH}/usr/local/lib64)

    if [ ${DYNAMIC} == "dynamic" ]; then
      echo "Installing platform libs"
      # Install platform specific libs to /usr/lib/python{X,Y}/lib-dynload
      cp ${PYTHON_SRC_LIB_STUBS} ${RUNENV_PATH}/${RUNENV_PYTHON_LIB_DYNLOAD}
      mkdir -p ${RUNENV_PATH}/opt/kontain
      tar -C /opt/kontain --exclude '*.a' --exclude '*.o' -cf - alpine-lib | tar -C ${RUNENV_PATH}/opt/kontain -xf -
    else
      echo "Installing platform libs as thumbstones..."
      # Install platform specific libs to /usr/lib/python{X,Y}/lib-dynload. The files are thumbstones.
      for stub in ${RUNENV_PYTHON_LIB_STUBS}
      do
         mkdir -p $(realpath -m ${RUNENV_PATH}/$(dirname $stub))
         touch $(realpath -m ${RUNENV_PATH}/$stub)
      done
    fi

    echo "Done..."
}

main
