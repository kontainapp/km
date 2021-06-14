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
# Script to run km tests with kkm in aws.
#
# Expects to run as Packer provisioner, and some var (e.g. IMAGE_VERSION and auth-related)
# are expected to be pre-set

[ "$TRACE" ] && set -x

# note: it timestamp is needed, pipe the script output
# via `ts` (from `moreutils`) or turn on  CI timestamps via UI
function log_message {
   echo -e "=== $@"
}

trap error_exit EXIT
function error_exit {
   log_message "Kernel logs:"
   dmesg
}


ROOT_DIR="${HOME}/src/"
mkdir -p ${ROOT_DIR}
cd ${ROOT_DIR}

# we only need KKM repo and Make system so not pulling in all submodules
echo Cloning KM repo. branch: ${SOURCE_BRANCH?undefined}
git clone --branch ${SOURCE_BRANCH} git@github.com:kontainapp/km.git
cd km
git submodule update --init kkm

log_message "Build, install and unit test KKM"
# Build has to happen here since it's kernel specific
make -C kkm/kkm
make -C kkm/test_kkm
sudo dmesg --clear
sudo insmod kkm/kkm/kkm.ko
./kkm/test_kkm/test_kkm

log_message "Pull testenv-image:${IMAGE_VERSION} and test with docker"
make -C cloud/azure login-cli
for dir in tests payloads
do
   make -C $dir pull-testenv-image
   make -C $dir test-withdocker HYPERVISOR_DEVICE=/dev/kkm
done
