#!/bin/sh -ex
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
