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

TOP := $(shell git rev-parse --show-toplevel)

BUILD ?= Release
VERS ?= v12.4.0
SHELL=/bin/bash

# Location of Node sources. When building in container, it is set in buildenv dockerfile.
NODETOP ?= node
# Node build directory - depends on where the sources are
NODEBUILD = ${NODETOP}/out/$(BUILD)
# Node build-produced artifacts - if building from blank, we copy them here
NODEOUT = node/out/$(BUILD)

# component id for docker images
COMPONENT := node

# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := ${NODEOUT}/node.km
TEST_KM := ${NODEOUT}/cctest.km

# name to be added to runtime image label(s)
PAYLOAD_NAME := NodeJs12.4

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
NODE_DISTRO_FILES := $(addprefix node/, deps doc test tools)

# either run all in 'blank' container, or local.
# in blank: link + tar + create shebang
# fromsrc local: link + create shebang
# regular local: just run docker
all:
ifneq ($(shell test -d node/.git && echo OK),OK)
	${DOCKER_RUN} -w /src -v ${TOP}:/src:Z kontain/buildenv-node-fedora /bin/bash -x -c "make -C payloads/node in-blank-container"
else
	./link-km.sh ${NODEBUILD} ${NODEOUT}
endif
	@# Create node file with ``#!${KM_BIN} --copyenv''. Invoking that will call ``${KM_BIN} node <args>''.
	@# km will add .km suffix. --copyenv is needed as test.py passes info to the tests via environment
	@# We need this so unmodified node tests can be executed
	echo "#!`realpath ${KM_BIN}` --copyenv" > ${NODEOUT}/node.sh && chmod +x ${NODEOUT}/node.sh
	ln -sf node.sh ${NODEOUT}/node

in-blank-container: ## invoked in blank contaier by ``make all''. DO NOT invoke manually
	mkdir -p ${NODEOUT}
	./link-km.sh ${NODEBUILD} ${NODEOUT}
	tar -C ${NODETOP}/.. --exclude='.tmp*' -cf - ${NODE_DISTRO_FILES} | tar -xf -

fromsrc:
	git clone https://github.com/nodejs/node.git -b ${VERS}
	cd ${NODETOP} && ./configure --gdb `[[ $BUILD == Debug ]] && echo -n --debug` && make -j`expr 2 \* $(nproc)` && make jstest
	mv ${NODEBUILD}/node ${NODEBUILD}/node.static
	./link-km.sh ${NODEBUILD} ${NODEOUT}

clobber:
	rm -rf node

clean:
	rm -rf $(RUNENV_PATH)
	images=`docker image ls -q  '${RUNENV_IMG}'` ; if [ -n "$$images" ] ; then docker rmi $$images ; fi
	@echo "Note - 'clean' doesn't remove ./node artifacts. use 'clobber' to force remove build artifacts"

test: ${PAYLOAD_KM} ${TEST_KM}
	scripts/test-run.sh test ${KM_BIN} ${PAYLOAD_KM} ${TEST_KM}

test-all: test
	scripts/test-run.sh test-all ${NODETOP} ${BUILD}

# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := scripts/test-run.sh test ./km ${PAYLOAD_KM} ${TEST_KM}
CONTAINER_TEST_ALL_CMD := ${CONTAINER_TEST_CMD} && scripts/test-run.sh test-all ${NODETOP} ${BUILD}

# Use existing small dir for buildenv image builds (saves on sending data to docker svc)
BUILDENV_PATH  := ./scripts

# Param for 'make validate-runenv-image'. Used to do a quick validation of runtime environment.
RUNENV_VALIDATE_DIR := scripts
RUNENV_VALIDATE_CMD := scripts/hello.js

# if something needs to be copied/modified before building runenv image in Docker,
# define this function
export define runenv_prep
	cp ${PAYLOAD_KM} ${RUNENV_PATH}
endef

include ${TOP}/make/images.mk
