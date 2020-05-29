#!/bin/bash
#
# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# script to run km tests with kkm in aws

[ "$TRACE" ] && set -x

export TERM=linux
ROOT_DIR="/home/fedora/src/"
LOG_DIR="/home/fedora/src/log/"

function error_exit {
   echo -e $1
   exit 0
}

if [ $# -ne 1 ]
then
   error_exit "Not enough parameters\nUsage:\n\t$0 branch"
fi

BRANCH_NAME=$1

rm -fr ${ROOT_DIR}
sudo rm -fr /opt/kontain/bin/
sudo rm -fr /opt/kontain/runtime/
sudo chmod 0777 /opt/kontain/
sudo mkdir -p /opt/kontain/runtime/
sudo chmod 0777 /opt/kontain/runtime/

mkdir -p ${ROOT_DIR}
cd ${ROOT_DIR}
mkdir -p ${LOG_DIR}

git clone --branch ${BRANCH_NAME} --recurse-submodules git@github.com:kontainapp/km.git >& ${LOG_DIR}/git-clone
if [ $? -ne 0 ]
then
   error_exit "Failed to clone repository"
fi
echo "KM repo clone success"

cd km
make -C kkm/kkm >& ${LOG_DIR}/build-kkm
if [ ! -f kkm/kkm/kkm.ko ]
then
   error_exit "buiding driver failed"
fi

make -C kkm/test_kkm >& ${LOG_DIR}/build-kkm-test
if [ ! -f kkm/test_kkm/test_kkm ]
then
   error_exit "building kkm test failed"
fi
echo "Building kkm and kkm_test success"

#make -C cloud/azure login
#make pull-buildenv-image
#make -C tests buildenv-local-fedora

make -j >& ${LOG_DIR}/build-km
if [ ! -f build/km/km ]
then
   error_exit "building km failed"
fi
echo "Building km success"

sudo insmod kkm/kkm/kkm.ko >& ${LOG_DIR}/insmod-kkm
if [ ! -e /dev/kkm ]
then
   error_exit "cannot find /dev/kkm"
fi
sudo chmod 0666 /dev/kkm >& ${LOG_DIR}/chmod-log
echo "KKM module init success"

make -C tests test USEVIRT=kkm >& ${LOG_DIR}/test-km
if [ $? -ne 0 ]
then
   error_exit "tests failed"
fi

grep "0 failures" ${LOG_DIR}/test-km >& /dev/null
if [ $? -ne 0 ]
then
   error_exit "tests failed"
fi
echo "tests successfull"

exit 0
