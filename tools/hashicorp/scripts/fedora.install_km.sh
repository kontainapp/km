#!/bin/bash -xe
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

# assumes $storage with kkm,run and kontain.tar.gz
# installs kontain, kkm, docker, podman
# We assume this script runs as superuser.

readonly storage="/tmp"

# dnf install kernel-headers-$(uname -r) -y 2>&1
dnf install -q -y kernel-headers gdb moby-engine

grubby --update-kernel=ALL --args="systemd.unified_cgroup_hierarchy=0"

# Install KKM... it will also validate hardware.
bash $storage/kkm.run --noprogress -- --force-install 2>&1

# Add Kontain to PATH
mkdir -p /opt/kontain && tar -C /opt/kontain -xzf $storage/kontain.tar.gz
echo "PATH=\$PATH:/opt/kontain/bin" >> ~vagrant/.bashrc

# Configure Docker and restart
usermod -aG docker,kvm vagrant
/opt/kontain/bin/docker_config.sh

# Config changes for podman
/opt/kontain/bin/podman_config.sh

