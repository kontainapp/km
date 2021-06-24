#!/bin/sh -ex
#
# Install stuff on "all you can eat" base image (cloud Ubuntu VM) to run vagrant/packer/virtualbox and build/test KM .
# Note that KM is not build here and instead recieved via testenv or release bundle, but KKM needs to build with correct header.
#
# expected to run as sudo

cat  /etc/os-release
# export DEBIAN_FRONTEND=noninteractive
echo 'debconf debconf/frontend select Noninteractive' | sudo debconf-set-selections
sudo apt-get update --yes -q

# kubectl
curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
kubectl version --client

# Docker repo
# sudo apt-get install apt-transport-https
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg
echo \
  "deb [arch=amd64 signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# 'az' cli
curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
az --version

# hashicorp repo
curl -fsSL https://apt.releases.hashicorp.com/gpg | sudo apt-key add -
sudo apt-add-repository "deb [arch=amd64] https://apt.releases.hashicorp.com $(lsb_release -cs) main"

# Install stuff
sudo apt update --yes -qq
sudo apt install --yes -qq git make makeself gcc linux-headers-$(uname -r) libelf-dev

sudo apt install --yes -qq docker-ce docker-ce-cli containerd.io
 sudo systemctl enable docker.service

if ! grep -q docker /etc/group ; then sudo groupadd docker ; fi
sudo usermod -aG docker $USER

# TODO - this needs to be in another base image (VagrantPreloadedBaseImage)
#  in the vast majority of cases these extra few GiB for boxes are not needed
# TODO box version and OS List is currently defined in multiple places
#  need to defined in makefile and pass around as env

# Install Hashicrop stuff and preload Vagrant boxes
sudo apt install --yes -qq vagrant packer virtualbox

# uncomment this to preinstall vagrant boxes
# preinstall_boxes="generic/ubuntu2010 generic/fedora32"
for box in $preinstall_boxes
do
   vagrant box add  $box --provider=virtualbox --box-version 3.2.24
done