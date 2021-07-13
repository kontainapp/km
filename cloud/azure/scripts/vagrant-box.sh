#!/bin/sh -ex
#
#  Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
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

git clone https://$GITHUB_TOKEN@github.com:/kontainapp/km -b ${SRC_BRANCH}
cd km
git submodule update --init kkm
mkdir build
cp /tmp/kkm.run /tmp/kontain.tar.gz build
# note: daemon.json is an repo so no need to copy it
make -C tools/hashicorp vm-images
## TBD: do a sanity test
# mkdir test ; cd test; vagrant init BOX_NAME; vagrant up ; vagrant ssh uick-test; vagrant halt; cd -

make -n -C tools/hashicorp upload-boxes
