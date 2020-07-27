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

set -x

export TERM=linux
ROOT_DIR="${HOME}/src/"
LOG_DIR="${ROOT_DIR}/log/"

function log_message {
   time_now=$(date +"%T-%N")
   echo -e $time_now:$@
}

function error_exit {
   log_message $1
   exit 0
}


if [ $# -ne 1 ]
then
   error_exit "Not enough parameters\nUsage:\n\t$0 branch"
fi

BRANCH_NAME=$1

umask 0
sudo chmod 0777 /opt/kontain/
DIRLIST="/opt/kontain/bin/ /opt/kontain/runtime/ ${ROOT_DIR} ${LOG_DIR}"
for entry in ${DIRLIST}
do
   sudo rm -fr $entry
   sudo mkdir -m 0777 -p $entry
done

cd ${ROOT_DIR}

git clone --branch ${BRANCH_NAME} --recurse-submodules git@github.com:kontainapp/km.git >& ${LOG_DIR}/git-clone
if [ $? -ne 0 ]
then
   error_exit "Failed to clone repository"
fi
log_message "KM repo clone success"

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
log_message "Building kkm and kkm_test success"

make -j >& ${LOG_DIR}/build-km
make all >& ${LOG_DIR}/build-km-all
if [ ! -f build/km/km ]
then
   error_exit "building km failed"
fi
log_message "Building km success"

sudo insmod kkm/kkm/kkm.ko >& ${LOG_DIR}/insmod-kkm
if [ ! -e /dev/kkm ]
then
   error_exit "cannot find /dev/kkm"
fi
sudo chmod 0666 /dev/kkm >& ${LOG_DIR}/chmod-log
log_message "KKM module init success"

make test USEVIRT=kkm >& ${LOG_DIR}/test-km
if [ $? -ne 0 ]
then
   error_exit "tests failed"
fi

grep "0 failures" ${LOG_DIR}/test-km >& /dev/null
if [ $? -ne 0 ]
then
   error_exit "tests failed"
fi
log_message "tests successfull"

exit 0
