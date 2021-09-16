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
# provison boxes as needed
# todo:
# check if I can vagrant provider here
# cp files from /tmp
# make sure Makefile does not try to build
# move boxes pre-load to local shell provisioner from Makefile
#      if (missing) vagrant box add generic/ubuntu2010 --provider=virtualbox
# pass GITHUB_TOKEN and VAGRANT_CLOUD_TOKEN
# clone
# preload proper boxes in base image
#

# Peparation so that we can do "make -C tools/hashicorp XXXX" below
git clone https://$GITHUB_TOKEN@github.com:/kontainapp/km
cd km
git checkout ${SRC_BRANCH}
git submodule update --init kkm
mkdir build
cp /tmp/kkm.run /tmp/kontain.tar.gz build
# note: daemon.json is in the repo so no need to copy it

echo "================= building vagrant boxes ===================="
make -C tools/hashicorp vm-images

# Sanity test each of the images produced by the vm-images make target
echo "================= testing vagrant boxes ===================="
make -C tools/hashicorp test-boxes

# Only save the boxes in the cloud for a release
if [ "${RELEASE_TAG}" != "" ]; then
   echo "================= publishing vagrant boxes ===================="
   make -C tools/hashicorp RELEASE_TAG=${RELEASE_TAG} upload-boxes
fi
