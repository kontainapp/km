#!/bin/bash
#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

# Use this script to create a rootfs for the python runenv image. We try to use
# as close to default as possible to avoid messing with PYTHONHOME and
# PYTHONPATH. The layout would be the follow. Using python:3.X-alpine as
# reference.
# /opt/kontain/bin/km (mapped into container through volume)
# /usr/local/bin/python3 -> /opt/kontain/bin/km
# /usr/local/bin/python3.km
# /usr/local/lib/python{X,Y} (cpython/Lib)
# /usr/local/lib/python{X,Y}/lib-dynload (cpython/build/lib.linux-x86_64-${X,Y})

[ "${TRACE}" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CURRENT=$(readlink -m $(dirname $0))
readonly PYTHON_SRC=$(readlink -m $1)
readonly RUNENV_PATH=$(readlink -m $2)
readonly PYTHON_VERSION=$3


readonly RUNENV_PYTHON_BIN=/usr/local/bin
readonly RUNENV_PYTHON_LIB=/usr/local/lib/python${PYTHON_VERSION}
readonly RUNENV_PYTHON_LIB_DYNLOAD=${RUNENV_PYTHON_LIB}/lib-dynload
readonly PYTHON_KM=${PYTHON_SRC}/python.km

readonly PYTHON_SRC_LIB=${PYTHON_SRC}/build/lib.linux-x86_64-${PYTHON_VERSION}
readonly PYTHON_SRC_LIB_STUBS=$(find ${PYTHON_SRC_LIB} -name '*.so')
readonly RUNENV_PYTHON_LIB_STUBS=$(echo ${PYTHON_SRC_LIB_STUBS} | sed "s|${PYTHON_SRC_LIB}|${RUNENV_PYTHON_LIB_DYNLOAD}|g")

readonly RUNENV_EXCLUDE='--exclude=*/test --exclude=*/__pycache__ --exclude *.exe --exclude *.whl'

function usage() {
    cat <<- EOF
usage: $PROGNAME <PYTHON SRC> <RUNENV PATH> <PYTHON VERSION>

Script used to create a rootfs for python runenv image.

EOF
}

function main() {
	rm -rf ${RUNENV_PATH} && mkdir -p ${RUNENV_PATH}
	mkdir -p $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_BIN})
	mkdir -p $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_LIB_DYNLOAD})
	mkdir -p ${RUNENV_PATH}/opt/kontain/bin

    # Install python.km into /usr/local/bin
    install -s ${PYTHON_KM} $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_BIN}/python3.km)
    # Create the symlink to be used with km shebang
	ln -s /opt/kontain/bin/km $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_BIN}/python3)

    # Install module libs to /usr/local/lib/python{X,Y}
	tar -cf - ${RUNENV_EXCLUDE} -C ${PYTHON_SRC}/Lib . | tar -C $(realpath -m ${RUNENV_PATH}/${RUNENV_PYTHON_LIB}) -xf -

    # Install platform specific libs to /usr/lib/python{X,Y}/lib-dynload. The files are thumbstone.
    for stub in ${RUNENV_PYTHON_LIB_STUBS}
    do
        mkdir -p $(realpath -m ${RUNENV_PATH}/$(dirname $stub))
        touch $(realpath -m ${RUNENV_PATH}/$stub)
    done
}

main