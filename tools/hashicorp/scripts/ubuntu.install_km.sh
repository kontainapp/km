#!/bin/bash -xe

# assumes $storage with kkm,run and kontain.tar.gz
# installs kontain , kkm, docker

readonly storage="/tmp"

# this seems to be needed on 'generic/ubuntu' box only
ln -sf /run/systemd/resolve/resolv.conf /etc/resolv.conf


# Install Docker
# docker repo
apt-get install -y -q apt-transport-https ca-certificates \
      curl gnupg-agent software-properties-common
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | apt-key add - 2>&1
# get ready for installs
apt-get update -y -q

# Install KKM... it will also validate hardware.
apt-get -y -q install linux-headers-$(uname -r) 2>&1
$storage/kkm.run 2>&1

# Install Kontain.
mkdir -p /opt/kontain && tar -C /opt/kontain -xzf $storage/kontain.tar.gz

add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
apt-get update -y -q

apt-get install -y -q docker-ce docker-ce-cli containerd.io

# Configure Docker and restart
for u in vagrant ubuntu ; do
   if getent passwd $u > /dev/null 2>&1; then
      echo "PATH=\$PATH:/opt/kontain/bin" >> $(eval echo ~$u/.bashrc)
      usermod -aG docker $u
   fi
done

cp $storage/daemon.json /etc/docker/daemon.json
systemctl reload-or-restart docker.service

# Add gdb, we usually need it
apt-get install gdb -y -q
