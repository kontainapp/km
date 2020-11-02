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
   touch ${LOG_DIR}/FAILED
   sudo dmesg -c > ${LOG_DIR}/kernel-run-logs
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

log_message "starting build"
BUILD_DIRS="kkm/kkm kkm/test_kkm . payloads/python payloads/node payloads/java"
for builddir in $BUILD_DIRS
do
   echo "################## build logs for $builddir" &>> ${LOG_DIR}/build-all
   make -C $builddir &>> ${LOG_DIR}/build-all
   log_message "$builddir build complete"
done

EXEC_LIST="kkm/kkm/kkm.ko kkm/test_kkm/test_kkm build/km/km"
for execfile in $EXEC_LIST
do
   if [ ! -f $execfile ]
   then
      error_exit "cannot find $execfile file"
   fi
done

# clear kernel logs
sudo dmesg -c > ${LOG_DIR}/kernel-boot-logs

echo "################## run logs for kkm" &>> ${LOG_DIR}/run-all
sudo insmod kkm/kkm/kkm.ko &>> ${LOG_DIR}/run-all
if [ ! -e /dev/kkm ]
then
   error_exit "cannot find /dev/kkm"
fi
log_message "KKM module init success"

TEST_DIRS="tests payloads/python payloads/node payloads/java"
log_message "starting test"
for testdir in $TEST_DIRS
do
   echo "################## run logs for $testdir tests" &>> ${LOG_DIR}/run-all
   make -C $testdir test USEVIRT=kkm &>> ${LOG_DIR}/run-all
   if [ $? -ne 0 ]
   then
      error_exit "$testdir tests failed"
   fi
   log_message "$testdir test complete"
done

sudo dmesg -c > ${LOG_DIR}/kernel-run-logs
grep "0 failures" ${LOG_DIR}/run-all >& /dev/null
if [ $? -ne 0 ]
then
   error_exit "tests failed"
fi
touch ${LOG_DIR}/PASSED
log_message "tests successfull"

exit 0
