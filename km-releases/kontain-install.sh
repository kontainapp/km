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
# Usage: ./kontain-install.sh [TAG | -u | file_path]
# -u = uninstall changes made by install
# file_path= path to local kontain.tar.gz in the format file:///full_path/to_file.tar.gz
#
#
set -e; [ "$TRACE" ] && set -x

# This script must be run as root.
[ `id -u` != "0" ] && echo "Must run as root" && exit 1

source /etc/os-release
[ "$ID" != "fedora" -a "$ID" != "ubuntu" -a "$ID" != "amzn" ] && echo "Unsupported linux distribution: $ID" && exit 1

readonly TAG=${1:-$(curl -L -s https://raw.githubusercontent.com/kontainapp/km/current/km-releases/current_release.txt)}
if [[ $TAG == latest ]] ; then
   readonly URL="https://github.com/kontainapp/km/releases/${TAG}/download/kontain.tar.gz"
elif [[ $TAG =~ ^file:* ]]; then
   readonly URL=$TAG
else
   readonly URL="https://github.com/kontainapp/km/releases/download/${TAG}/kontain.tar.gz"
fi

readonly PREFIX="/opt/kontain"

# UNINSTALL
if [ $# -eq 1 -a "$1" = "-u" ]; then
   echo "Removing kontain"

   # Remove kontain config changes for docker and podman
   bash $PREFIX/bin/podman_config.sh -u
   bash $PREFIX/bin/docker_config.sh -u

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
     # set 0666 permissions on /dev/kvm
     echo 'KERNEL=="kvm", GROUP="kvm", MODE="0666"' > /tmp/rules
     mv /tmp/rules /etc/udev/rules.d/99-perm.rules
     udevadm control --reload-rules && sudo udevadm trigger
     validate=1
   fi
}

function get_bundle {
   mkdir -p $PREFIX
   echo "Pulling $URL..."
   curl -L -s $URL | tar -C ${PREFIX} -xzf -
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
   bash $PREFIX/bin/podman_config.sh
   bash $PREFIX/bin/docker_config.sh
}

# main
check_args
check_prereqs
get_bundle
config_container_runner
