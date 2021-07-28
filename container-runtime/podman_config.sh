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


#
# A little script to get needed linux packages, change podman config files, and add a small
# kontain selinux policy to allow podman to run containers using krun and km.
#

# podman config file locations
CONTAINERS_CONF=/usr/share/containers/containers.conf
HOME_CONTAINERS_CONF=~/.config/containers/containers.conf
ETC_CONTAINERS_CONF=/etc/containers/containers.conf
DOCKER_INIT=/usr/libexec/docker/docker-init
KRUN_PATH=/opt/kontain/bin/krun
KM_PATH=/opt/kontain/bin/km
KM_SELINUX_CONTEXT="system_u:object_r:bin_t:s0"

# Either do it or just print the commands we think should be run.
#DEBUG=echo
DEBUG=""

linuxdist=`grep "^ID=" /etc/os-release`

# Install podman and policy build tools
echo "Installing podman and selinux packages"
if test "$linuxdist" =  "ID=fedora"
then
   $DEBUG sudo dnf install -y -q --refresh podman selinux-policy-devel
elif test "$linuxdist" = "ID=ubuntu"
then
   $DEBUG sudo apt-get update
   $DEBUG sudo apt-get install -y -q podman selinux-policy-dev
else
   echo "Unsupported linux distributionn $linuxdist"
   exit 1
fi


# set init_path in the [containers] section of ~/.config/containers/containers.conf
# We don't ensure we add this in the [containers] section.
# We hope the init_path command line comment exists and is in the proper section.
echo
if ! grep -q "^ *init_path" $HOME_CONTAINERS_CONF
then
   # no init_path, use the docker init
   $DEBUG sed --in-place -e "/^# *init_path/ainit_path = \"$DOCKER_INIT\"" $HOME_CONTAINERS_CONF
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
   $DEBUG sed --in-place -e "/^\[engine.runtimes\]/akrun = [\n   \"$KRUN_PATH\",\n]\n" $HOME_CONTAINERS_CONF
else
   echo "runtime krun already configured in $HOME_CONTAINERS_CONF"
   grep -A 3 "^ *krun =" $HOME_CONTAINERS_CONF
fi


# set selinux context on /opt/kontain/bin/km
echo
$DEBUG chcon $KM_SELINUX_CONTEXT $KM_PATH


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

$DEBUG ln -sf /usr/share/selinux/devel/Makefile
$DEBUG make
$DEBUG sudo make reload
popd || exit

