# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Makefile for building and packing Node.js payload runtime for KM

TOP := $(shell git rev-parse --show-cdup)
BUILD ?= Release
VERS ?= v12.4.0
NODETOP ?= node

NODEBUILD = ${NODETOP}/out/$(BUILD)

# KM monitor path
KM_BIN=${TOP}/build/km/km
# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := ${NODEBUILD}/node.km
TEST_KM := ${NODEBUILD}/cctest.km
# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := node-km
# name to be added to label(s)
PAYLOAD_NAME := NodeJs12.4

# a list to be passed to tar to be packed into container image - TODO !
PAYLOAD_FILES := --exclude='*/test/*' scripts $(PAYLOAD_KM)

all: ${PAYLOAD_KM} ${TEST_KM}

${PAYLOAD_KM} ${TEST_KM}: ${NODEBUILD}
	./link-km.sh ${NODEBUILD}
	@# Create node file with ``#!${KM_BIN}''. Invonking that will call ``${KM_BIN} node <args>'', km will add .km suffix
	@# We need this so unmodified node tests can be executed
	echo "#!`realpath ${KM_BIN}` --copyenv" > ${NODEBUILD}/node.sh
	chmod +x ${NODEBUILD}/node.sh
	ln -sf node.sh ${NODEBUILD}/node

${NODEBUILD}: | ${NODETOP}
	cd ${NODETOP} && ./configure --gdb `[[ $BUILD == Debug ]] && echo -n --debug` && make -j`expr 2 \* $(nproc)` && make jstest
	mv ${NODEBUILD}/node ${NODEBUILD}/node.static

${NODETOP}:
	git clone https://github.com/nodejs/node.git -b ${VERS}

# TODO: we need to modify and use 'make -j4 test' in Node, per 'pull-requests.md'
test-all: ${PAYLOAD_KM} ${TEST_KM}
	${KM_BIN} ${PAYLOAD_KM} ./scripts/hello.js
	@echo noop.js - expecting exit with code 22:
	${KM_BIN} ${PAYLOAD_KM} ./scripts/noop.js ; [ $$? -eq 22 ]
	${KM_BIN} ${PAYLOAD_KM} ./scripts/micro-srv.js & sleep 0.5 ; curl localhost:8080
	-curl -X POST localhost:8080 # force the srv to exit and ignore curl 'empty reply'
	${KM_BIN} ${TEST_KM} --gtest_filter="*"
	@cat skip_* | tr '\n' ',' > /tmp/skip_tests
	cd ${NODETOP} && /bin/python2.7 tools/test.py -J --mode=`echo -n ${BUILD} | tr '[A-Z]' '[a-z]'` --skip-tests=`cat /tmp/skip_tests` default addons js-native-api node-api

include ${TOP}make/distro.mk