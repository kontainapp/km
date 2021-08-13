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

# A little script to get needed linux packages, change podman config files, and add a small
# kontain selinux policy to allow podman to run containers using krun and km.

# exit if any command fails, display the commands if TRACE has a value.
set -e ; [ "$TRACE" ] && set -x

# Should be run as root.
[ `id -u` != "0" ] && echo "Must run as root" && exit 1

# file locations
GETENFORCEPATH=/usr/sbin/getenforce
DOCKER_INIT_FEDORA=/usr/libexec/docker/docker-init
DOCKER_INIT_UBUNTU=/usr/bin/docker-init
KRUN_PATH=/opt/kontain/bin/krun
KM_PATH=/opt/kontain/bin/km
KM_SELINUX_CONTEXT="system_u:object_r:bin_t:s0"
CONTAINERS_CONF=/usr/share/containers/containers.conf
ETC_CONTAINERS_CONF=/etc/containers/containers.conf
# if running under sudo, update the invokers containers.conf, not root's
# Note that the shell nazi's think eval to expand ~ is evil but I can't find a better way
HOME_CONTAINERS_CONF=`eval echo ~${SUDO_USER}/.config/containers/containers.conf`
mkdir -p `dirname $HOME_CONTAINERS_CONF`

# return success if selinux is present and enabled, otherwise return fail
function selinux_is_enabled()
{
   if [ -e "${GETENFORCEPATH}" ]
   then
      if [ "`${GETENFORCEPATH}`" != "Disabled" ]
      then
         return 0
      fi
   fi
   return 1
}

# Set variables to know what kind of system this is.
. /etc/os-release

# docker-init is in different directories for different distributions
if [ "$ID" = "fedora" ]; then
   DOCKER_INIT=$DOCKER_INIT_FEDORA
elif [ "$ID" = "ubuntu" ]; then
   DOCKER_INIT=$DOCKER_INIT_UBUNTU
else
   echo "Unsupported linux distributionn $ID"
   exit 1
fi

# Install podman and policy build tools
echo "Installing podman and selinux packages"
if test "$ID" =  "fedora"
then
   dnf install -y -q --refresh podman selinux-policy-devel
elif test "$ID" = "ubuntu"
then
   apt-get update 
   apt-get install -y wget gnupg2
   echo "deb http://download.opensuse.org/repositories/devel:/kubic:/libcontainers:/stable/xUbuntu_${VERSION_ID}/ /" > /etc/apt/sources.list.d/devel:kubic:libcontainers:stable.list
   wget -nv https://download.opensuse.org/repositories/devel:kubic:libcontainers:stable/xUbuntu_${VERSION_ID}/Release.key -O- | apt-key add -
   apt-get update
   apt-get install -y  podman
   # ubuntu doesn't use selinux by default.
   #apt-get install -y -q selinux-policy-dev
else
   echo "Unsupported linux distributionn $ID"
   exit 1
fi


# If the user's containers.conf is missing give them a bare bones version
echo
if [ ! -e $HOME_CONTAINERS_CONF ]
then
   cat <<EOF >$HOME_CONTAINERS_CONF
[containers]
init_path = "$DOCKER_INIT"

[network]

[engine]

[engine.runtimes]
krun = [
        "/opt/kontain/bin/krun"
]
EOF
   if test "$SUDO_USER" != ""
   then
      chown $SUDO_USER $HOME_CONTAINERS_CONF
      chgrp `id -g $SUDO_USER` $HOME_CONTAINERS_CONF
   fi
fi

# set init_path in the [containers] section of ~/.config/containers/containers.conf
if ! grep -q "^ *init_path" $HOME_CONTAINERS_CONF
then
   # no init_path, use the docker init
   sed --in-place -e "/^\[containers\]/ainit_path = \"$DOCKER_INIT\"" $HOME_CONTAINERS_CONF
else
   if ! grep -q "^ *init_path *= *\"$DOCKER_INIT\"" $HOME_CONTAINERS_CONF
   then
      echo "Leaving init_path statement in $HOME_CONTAINERS_CONF unchanged, you may need to change it to $DOCKER_INIT"
      echo "Existing init_path:"
      grep "^ *init_path" $HOME_CONTAINERS_CONF
   else
      echo "init_path = \"$DOCKER_INIT\"" is already in $HOME_CONTAINERS_CONF
   fi
fi


# add krun to ~/.config/containers/containers.conf at the beginning of the [engine.runtimes] section
echo
if ! grep -q "^ *krun =" $HOME_CONTAINERS_CONF
then
   sed --in-place -e "/^\[engine.runtimes\]/akrun = [\n   \"$KRUN_PATH\",\n]\n" $HOME_CONTAINERS_CONF
else
   echo "runtime krun already configured in $HOME_CONTAINERS_CONF"
   grep -A 3 "^ *krun =" $HOME_CONTAINERS_CONF
fi

# If the system has selinux enabled setup our policy
if selinux_is_enabled
then
   echo "selinux is present on this system"

   # set selinux context on /opt/kontain/bin/km
   chcon $KM_SELINUX_CONTEXT $KM_PATH

   # Add kontain selinux policy adjustments
   POLDIR=/tmp/kontain_selinux_policy
   mkdir -p $POLDIR
   pushd $POLDIR || exit
   cat <<EOF >kontain_selinux_policy.te
module kontain_selinux_policy 1.0.0;

require {
  type container_t;
  type kvm_device_t;
  class chr_file { append getattr ioctl lock open read write };
}

allow container_t kvm_device_t:chr_file { append getattr ioctl lock open read write };

# You may get errors when this is "compiled".
# This is a known issue, see:
# https://bugzilla.redhat.com/show_bug.cgi?id=1861968
EOF

   ln -sf /usr/share/selinux/devel/Makefile
   make
   make reload
   popd || exit
else
   echo "selinux not enabled on this system"
fi
