#!/bin/bash -e
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
# Script to run km tests or validate-runenv-images, depending on ${TARGET}.
# Intented to run on a remote VM and is controlled by the env vars below

[ "$TRACE" ] && set -x

# 1. Check parameters and prep environment

# The following env is expected to be pre-set by the invoker and needed for code called from here:
echo IMAGE_VERSION=${IMAGE_VERSION?please set IMAGE_VERSION}   # image tag for pulling testenv or runenv
echo HYPERVISOR_DEVICE=${HYPERVISOR_DEVICE?please set HYPERVISOR_DEVICE} # /dev/kvm or /dev/kkm
echo SRC_BRANCH=${SRC_BRANCH?please set SRC_BRANCH} # source code to check out for make system
echo SP_TENANT=${SP_TENANT?please set SP\* env vars for az login}
echo LOCATION=${LOCATION?Please set LOCATION env var to KM source dir where the test needs to run}
# Log environment
env

rm -rf km
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
   modinfo kkm >& /dev/null && sudo rmmod kkm
   sudo insmod kkm/kkm/kkm.ko
   ./kkm/test_kkm/test_kkm
else
   echo "KERNEL=="kvm", GROUP="kvm", MODE="0666"" > /tmp/rules
   sudo mv /tmp/rules /etc/udev/rules.d/99-perm.rules
   sudo udevadm control --reload-rules && sudo udevadm trigger
fi

echo Checking for kkm or kvm modules
lsmod | grep -i k.m
ls -l /dev/k*

# 2. Get pre-requisite images and run tests

make -C cloud/azure login-cli

set -x
case "${TARGET}" in
   test|test-all)
      make -C $LOCATION pull-testenv-image
      make -C $LOCATION ${TARGET}-withdocker
      ;;
   validate-runenv-image)
      sudo mkdir -p /opt/kontain/bin
      sudo mv -t /opt/kontain/bin /tmp/km /tmp/krun
      sudo chmod +x /opt/kontain/bin/km /opt/kontain/bin/krun
      sudo bash ./container-runtime/podman_config.sh
      sudo bash ./container-runtime/docker_config.sh
      make -C $LOCATION pull-demo-runenv-image
      make -C $LOCATION ${TARGET}
      ;;
   *)
      echo "Unknown TARGET '$TARGET'. Choices are: test, test-all, validate-runenv-image"
      false
      ;;
esac

echo '=== Test completed.'
