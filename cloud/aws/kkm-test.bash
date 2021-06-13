#!/bin/bash -e
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
ROOT_DIR="${HOME}/src/"
LOG_DIR="${ROOT_DIR}/log/"

function log_message {
   time_now=$(date +"%T-%N")
   echo -e $time_now:$@
}

trap error_exit EXIT
function error_exit {
   echo ==== Kernel logs
   dmesg
}

sudo dmesg --clear

umask 0
sudo chmod 0777 /opt/kontain/
DIRLIST="/opt/kontain/bin/ /opt/kontain/runtime/ ${ROOT_DIR} ${LOG_DIR}"
for entry in ${DIRLIST}
do
   sudo rm -fr $entry
   sudo mkdir -m 0777 -p $entry
done

cd ${ROOT_DIR}
echo cloning km with branch=${SOURCE_BRANCH?undefined} ...
git clone --branch ${SOURCE_BRANCH} --recurse-submodules git@github.com:kontainapp/km.git

cd km
make -C cloud/azure login-cli

# Note: build here is useless.
# We should just pull-testenv-image and then 'make test-withdocker' for the proper IMAGE_VERSION which
# should be passed in env vars
log_message "starting build"
make pull-buildenv-image
BUILD_DIRS="kkm/kkm kkm/test_kkm . payloads/python payloads/node payloads/java"
for builddir in $BUILD_DIRS
do
   log_message "###### build for $builddir/"
   make -j $(nproc) -C $builddir
done

# Note: instead of build. we should just grab pre-built artifacts and pass it to the job in CI,
# and then drop on EC2 instance with "file" provisioner
log_message "Checking presense of build results"
ls kkm/kkm/kkm.ko kkm/test_kkm/test_kkm build/km/km

# Install and test - that's really all that is needed here
sudo insmod kkm/kkm/kkm.ko
ls -l /dev/kkm  # this will break if the file is not there

TEST_DIRS="tests payloads/python payloads/node payloads/java"
log_message "starting test"
for testdir in $TEST_DIRS
do
   make -C $testdir test USEVIRT=kkm
done
