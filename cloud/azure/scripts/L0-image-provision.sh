#!/bin/sh -ex
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
# Install stuff on "all you can eat" base image (cloud Ubuntu VM) to run vagrant/packer/virtualbox and build/test KM .
# Note that KM is not build here and instead recieved via testenv or release bundle, but KKM needs to build with correct header.
#
# expected to run as sudo

cat /etc/os-release
# export DEBIAN_FRONTEND=noninteractive
echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections

# kubectl
curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
kubectl version --client

# Docker repo
# apt-get install apt-transport-https
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg
echo \
  "deb [arch=amd64 signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) stable" | tee /etc/apt/sources.list.d/docker.list > /dev/null

# hashicorp repo
curl -fsSL https://apt.releases.hashicorp.com/gpg | apt-key add -
apt-add-repository "deb [arch=amd64] https://apt.releases.hashicorp.com $(lsb_release -cs) main"

# Install stuff
apt-get update --yes -q
apt-get install --yes -q git make makeself gcc linux-headers-$(uname -r) libelf-dev \
                         docker-ce docker-ce-cli containerd.io azure-cli \
                         vagrant packer virtualbox

systemctl enable docker.service

if ! grep -q docker /etc/group ; then groupadd docker ; fi
usermod -aG docker $USER

# TODO - this needs to be in another base image (VagrantPreloadedBaseImage)
#  in the vast majority of cases these extra few GiB for boxes are not needed
# TODO box version and OS List is currently defined in multiple places
#  need to defined in makefile and pass around as env

# uncomment this to preinstall vagrant boxes
# preinstall_boxes="generic/ubuntu2010 generic/fedora32"
for box in $preinstall_boxes
do
   vagrant box add  $box --provider=virtualbox --box-version 3.2.24
done
