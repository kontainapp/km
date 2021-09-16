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
# Install Kontain release on a Linux box. Assumes root.
#
# Usage: ./kontain-install.sh [TAG | -u]
# -u = uninstall changes made by install
#
set -e; [ "$TRACE" ] && set -x

source /etc/os-release
[ "$ID" != "fedora" -a "$ID" != "ubuntu" ] && echo "Unsupported linux distribution: $ID" && exit 1

export DEFAULT_TAG=latest
readonly TAG=${1:-$DEFAULT_TAG}
if [[ $TAG = latest ]] ; then
   readonly URL="https://github.com/kontainapp/km/releases/${TAG}/download/kontain.tar.gz"
else
   readonly URL="https://github.com/kontainapp/km/releases/download/${TAG}/kontain.tar.gz"
fi
readonly PREFIX="/opt/kontain"

# UNINSTALL
if [ $# -eq 1 -a "$1" = "-u" ]; then
   echo "Removing kontain"

   # Remove kontain config changes for docker and podman
   bash /opt/kontain/bin/podman_config.sh -u
   bash /opt/kontain/bin/docker_config.sh -u

   # Unload kernel modules?

   rm -rf ${PREFIX}
   exit 0
fi

function check_args {
   # "check-arg: Noop for now"
   true
}

function warning {
   echo "*** Warning:  $*"
}

function error {
   echo "*** Error:  $*"
   exit 1
}

pkgs_missing=0
function check_packages {
   echo "Checking that packages $NEEDED_PACKAGES are present"
   for i in $NEEDED_PACKAGES; do
      if ! rpm -qa | grep $i -q; then
         pkgs_missing=1
         warning "Package $i is required and is not installed."
      fi
   done
}

validate=0
function check_prereqs {
   if [ $(uname) != Linux ]; then
      error "Kontain requires a Linux distribution, e.g. Ubuntu 20 or Fedora 32. Current plaform: $(uname)"
   fi

   # check for PREFIX
   if [[ ! -d $PREFIX || ! -w $PREFIX ]]; then
      error "$PREFIX does not exist or not writeable. Use 'sudo mkdir -p $PREFIX ; sudo chown $(whoami) $PREFIX'"
   fi

   if ! command -v gcc; then
      warning "GCC is not found, only pre-linked unikernels can be used on this machine"
   fi

   # check and warn about kvm and version
   if ! lsmod | grep -q kvm; then
      if ! lsmod | grep -q kkm; then
         warning "No virtualization module (KKM or KVM) is found"
         validate=0
      elif [ ! -w /dev/kkm ]; then
         warning "KKM module is present but /dev/kkm is missing or not writeable"
      else
         validate=1
      fi
   elif [ ! -w /dev/kvm ]; then
      warning "KVM module is present but /dev/kvm is missing or not writeable"
   else
      validate=1
   fi

}

function get_bundle {
   mkdir -p $PREFIX
   echo "Pulling $URL..."
   wget $URL --output-document - -q | tar -C ${PREFIX} -xzf -
   echo Done.
   if [ $validate == 1 ]; then
      $PREFIX/bin/km $PREFIX/tests/hello_test.km Hello World
   else
      echo Install either KVM or KKM Module and then validate installation by running
      echo $PREFIX/bin/km $PREFIX/tests/hello_test.km Hello World
   fi
   if [ $pkgs_missing -ne 0 ]; then
      echo "Some packages need to be installed to use all functionality"
   fi
}

function config_container_runner {
   bash /opt/kontain/bin/podman_config.sh
   bash /opt/kontain/bin/docker_config.sh
}

# main
check_args
check_prereqs
get_bundle
config_container_runner
