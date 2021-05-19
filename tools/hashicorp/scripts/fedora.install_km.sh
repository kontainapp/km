#!/bin/bash -xe

# assumes $storage with kkm,run and kontain.tar.gz
# installs kontain , kkm, docker

readonly storage="/tmp"

# Install KKM... it will also validate hardware.
# dnf install kernel-headers-$(uname -r) -y 2>&1
dnf install kernel-headers -y 2>&1
$storage/kkm.run 2>&1

# Install Docker
dnf install -y moby-engine

# Add Kontain to PATH
mkdir -p /opt/kontain && tar -C /opt/kontain -xzf $storage/kontain.tar.gz
echo "PATH=\$PATH:/opt/kontain/bin" >> ~vagrant/.bashrc

# Add gdb, we usually need it
dnf install -y gdb

# Configure Docker and restart
usermod -aG docker vagrant
mkdir /etc/docker && cp $storage/daemon.json /etc/docker/daemon.json
systemctl reload-or-restart docker.service

