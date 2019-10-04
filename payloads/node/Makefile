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

# Location of Node sources. When building in container, it is set in buildenv dockerfile.
NODETOP ?= node
# Node build directory - depends on where the sources are
NODEBUILD = ${NODETOP}/out/$(BUILD)
# Node build-produced artifacts - if building from blank, we copy them here at the end
NODEOUT = node/out/$(BUILD)

# KM monitor path
KM_BIN := ${TOP}/build/km/km
# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := ${NODEOUT}/node.km
TEST_KM := ${NODEOUT}/cctest.km
# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := node-km
# name to be added to label(s)
PAYLOAD_NAME := NodeJs12.4
# TODO: a list to be passed to tar to be packed into container image
PAYLOAD_FILES := --exclude='*/test/*' scripts $(PAYLOAD_KM)

NODE_DISTRO_FILES := ${PAYLOAD_KM} ${TEST_KM} $(addprefix node/, deps doc test tools)


DOCKER_BUILD := docker build --network=host
DOCKER_RUN := docker run -t --rm --ulimit nofile=1024:1024

# either run all in 'blank' container, or local.
# in blank: link + tar + create shebang
# fromsrc local: link + create shebang
# regular local: just run docker
all:
ifeq ($(shell test -d node/.git && echo OK),OK)
	./link-km.sh ${NODEBUILD}
endif
ifneq ($(dir $(realpath ${NODETOP})),${CURDIR}/)
	./link-km.sh ${NODEBUILD}
	tar -C ${NODETOP}/.. -cf - ${NODE_DISTRO_FILES} | tar -xf -
endif
	@# Create node file with ``#!${KM_BIN} --copyenv''. Invoking that will call ``${KM_BIN} node <args>''.
	@# km will add .km suffix. --copyenv is needed as test.py passes info to the tests via environment
	@# We need this so unmodified node tests can be executed
	echo "#!`realpath ${KM_BIN}` --copyenv" > ${NODEOUT}/node.sh && chmod +x ${NODEOUT}/node.sh
	ln -sf node.sh ${NODEOUT}/node

fromsrc:
	git clone https://github.com/nodejs/node.git -b ${VERS}
	cd ${NODETOP} && ./configure --gdb `[[ $BUILD == Debug ]] && echo -n --debug` && make -j`expr 2 \* $(nproc)` && make jstest
	mv ${NODEBUILD}/node ${NODEBUILD}/node.static

clobber:
	rm -rf node

test: ${PAYLOAD_KM} ${TEST_KM}
	${KM_BIN} ${PAYLOAD_KM} ./scripts/hello.js
	@echo noop.js - expecting exit with code 22:
	${KM_BIN} ${PAYLOAD_KM} ./scripts/noop.js ; [ $$? -eq 22 ]
	${KM_BIN} ${PAYLOAD_KM} ./scripts/micro-srv.js & sleep 0.5 ; curl localhost:8080
	-curl -X POST localhost:8080 # force the srv to exit and ignore curl 'empty reply'
	${KM_BIN} ${TEST_KM} --gtest_filter="*"

test-all: test
	cd node && /bin/python2.7 tools/test.py -J --mode=`echo -n ${BUILD} | tr '[A-Z]' '[a-z]'` --skip-tests=`cat ../skip_* | tr '\n' ','` default addons js-native-api node-api

mk-test-image: ${KM_BIN}
	cp ${KM_BIN} node
	${DOCKER_BUILD} -t km-test-node -f test-fedora.dockerfile .

mk-buildenv-image:
	${DOCKER_BUILD} -t buildenv-node-fedora -f buildenv-fedora.dockerfile .

include ${TOP}make/distro.mk
