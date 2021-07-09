#
# Copyright 2021 Kontain Inc.
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
# Makefile for building and packing Node.js payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

BUILD ?= Release
VERS ?= v12.4.0

# Location of Node sources. When building in container, it is set in buildenv dockerfile.
NODETOP ?= node
# Node build directory - depends on where the sources are
NODEBUILD = ${NODETOP}/out/$(BUILD)
# Node build-produced artifacts - if building from blank, we copy them here
NODEOUT = node/out/$(BUILD)

# component id for docker images
COMPONENT := node
NODE_REPO := https://github.com/nodejs/node.git

# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := ${NODEOUT}/node.km
TEST_KM := ${NODEOUT}/cctest.km

# name to be added to runtime image label(s)
PAYLOAD_NAME := NodeJs12.4

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
NODE_DISTRO_FILES := $(addprefix node/, deps doc test tools)

# Either run all in 'blank' container, or local.
# in blank: link + tar + create shebang
# fromsrc local: link + create shebang
# regular local: just run docker
all:
ifneq ($(shell test -d node/.git && echo OK),OK)
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:/src:Z \
		-v ${KM_OPT_RT}:${KM_OPT_RT}:Z  \
		-v ${KM_OPT_LIB}:${KM_OPT_LIB}:Z \
		-w /src \
		$(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION) \
		/bin/bash -x -c "make -C payloads/node in-blank-container"
else
	./link-km.sh ${NODEBUILD} ${NODEOUT}
endif
	ln -sf `realpath ${KM_BIN}` ${NODEOUT}/node

in-blank-container: ## invoked in blank contaier by ``make all''. DO NOT invoke manually
	mkdir -p ${NODEOUT}
	./link-km.sh ${NODEBUILD} ${NODEOUT}
	tar -C ${NODETOP}/.. --exclude='.tmp*' -cf - ${NODE_DISTRO_FILES} | tar -xf -

${NODE_TOP}: |
	git clone ${NODE_REPO} -b ${VERS}

${NODE_TOP}/config.mk: ${NODE_TOP}
	cd ${NODETOP} && ./configure --gdb `[[ ${BUILD} == Debug ]] && echo -n --debug`

fromsrc:
	cd ${NODETOP} && make -j`expr 2 \* $$(nproc)` && make jstest
	mv ${NODEBUILD}/node ${NODEBUILD}/node.static
	./link-km.sh ${NODEBUILD} ${NODEOUT}
	ln -sf `realpath ${KM_BIN}` ${NODEOUT}/node

clobber:
	rm -rf node

clean:
	rm -rf $(RUNENV_PATH)
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
RUNENV_VALIDATE_CMD := ("/scripts/hello.js")

# if something needs to be copied/modified before building runenv image in Docker,
# define this function
export define runenv_prep
	cp ${PAYLOAD_KM} ${RUNENV_PATH}/$(basename $(notdir ${PAYLOAD_KM}))
endef

RELEASE_TAG := node node-12 node-${VERS}

include ${TOP}/make/images.mk
