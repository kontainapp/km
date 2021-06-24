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
# Script to run km tests on a VM. Generally runs on a remote VM and is controlled by the env vars below
#
[ "$TRACE" ] && set -x


# 1. Check parameters and prep environment
# ========================================

# The following env is expected to be pre-set by the invoker and needed for code called from here:
echo IMAGE_VERSION=${IMAGE_VERSION?please set IMAGE_VERSION}   # image tag for pulling testenv or runenv
echo HYPERVISOR_DEVICE=${HYPERVISOR_DEVICE?please set HYPERVISOR_DEVICE} # /dev/kvm or /dev/kkm
echo SRC_BRANCH=${SRC_BRANCH?please set SRC_BRANCH} # source code to check out for make system
echo SP_TENANT=${SP_TENANT?please set SP\* env vars for az login}
echo LOCATION=${LOCATION?Please set LOCATION env var to KM source dir where the test needs to run}
# Useful debug info (sans secrets)
env # | grep -v SP_ | grep -v GITHUB_TOKEN

# we only need KKM repo and Make system so not pulling in all submodules
echo Cloning KM repo. branch: ${SRC_BRANCH}
git clone https://$GITHUB_TOKEN@github.com:/kontainapp/km -b ${SRC_BRANCH}
cd km


if [ "${HYPERVISOR_DEVICE}" == "/dev/kkm" ] ; then
   git submodule update --init kkm
   echo =====  "Build, install and unit test KKM"
   # Build has to happen here since it's kernel specific
   make -C kkm/kkm
   make -C kkm/test_kkm
   sudo insmod kkm/kkm/kkm.ko
   ./kkm/test_kkm/test_kkm
else
   sudo chmod a+rw /dev/kvm
fi

echo Checking for kkm or kvm modules
lsmod  | grep -i k.m
ls -l /dev/k*

# 2. Get pre-requisite images and run tests
#==========================================

make -C cloud/azure login-cli

# build targets and pre-requisite target for make
set -x
case "${TARGET}" in
   test|test-all)
      make -C $LOCATION pull-testenv-image
      make -C $LOCATION ${TARGET}-withdocker
      ;;
   validate-runenv-image)
      # for runenv images we need KM installed first !
      # TODO: Pass it via Artifacts instead of building here on Ubuntu
      #       Specifically: download artifact action, then "file" provisioner to send it to the VM
      sudo mkdir -p /opt/kontain/bin ; sudo chmod 777 /opt/kontain/bin
      make -C km -j $(nproc)
      make -C $LOCATION pull-runenv-image
      make -C $LOCATION ${TARGET}
      ;;
   *)
      echo "Unknown TARGET '$TARGET'. Choices are: test, test-all, validate-runenv-image"
      false
      ;;
esac

echo '=== Test completed.'