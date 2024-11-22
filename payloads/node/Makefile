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
# Makefile for building and packing Node.js payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

OS_NAME=$(shell awk 'BEGIN { FS = "=" } $$1 == "ID" { NAME=$$2;} $$1 == "VERSION_ID" { VERS=$$2; } END { printf "%s%s", NAME, VERS }' /etc/os-release)

ifeq ($(OS_NAME),fedora40)
	VERS ?= v20.12.2
	PAYLOAD_NAME := NodeJs20.12
else ifeq ($(OS_NAME),fedora39)
	VERS ?= v20.8
	PAYLOAD_NAME := NodeJs20.8
else
	VERS ?= v16.13.1
	PAYLOAD_NAME := NodeJs16.13
endif

BUILD ?= Release

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
PAYLOAD_KM := ${CURDIR}/${NODEOUT}/node.km
CCTEST_KM := ${CURDIR}/${NODEOUT}/cctest.km

# name to be added to runtime image label(s)

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
NODE_DISTRO_FILES := $(addprefix node/, deps doc test tools benchmark)

# if something needs to be copied/modified before building runenv image in Docker,
# define this function
export define runenv_prep
	cp ${PAYLOAD_KM} ${RUNENV_PATH}/$(basename $(notdir ${PAYLOAD_KM}))
endef

include ${TOP}/make/images.mk

# Either run all in 'blank' container, or local.
# in blank: link + tar + create shebang
# fromsrc local: link + create shebang
# regular local: just run docker
all:
ifneq ($(shell test -d node/.git && echo OK),OK)
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:/src:z \
		${KM_OPT_VOLUME} \
		-w /src \
		$(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION) \
		/bin/bash -x -c "make -C payloads/node in-blank-container"
else
	./link-km.sh ${NODEBUILD} ${NODEOUT} ${OS_NAME}
endif

in-blank-container: ## invoked in blank contaier by ``make all''. DO NOT invoke manually
	mkdir -p ${NODEOUT}
	./link-km.sh ${NODEBUILD} ${NODEOUT} ${OS_NAME}
	tar -C ${NODETOP}/.. --exclude='.tmp*' -cf - ${NODE_DISTRO_FILES} | tar -xf -

${NODETOP}: |
	git clone ${NODE_REPO} -b ${VERS}

${NODETOP}/config.mk: | ${NODETOP}
	cd ${NODETOP} && ./configure --gdb `[[ ${BUILD} == Debug ]] && echo -n --debug`


#	cd ${NODETOP} && make -j`expr 2 \* $$(nproc)` && make jstest CI_SKIP_TESTS="parallel/test-https-selfsigned-no-keycertsign-no-crash.js" 

fromsrc: ${NODETOP}/config.mk
	make -C ${NODETOP} -j`expr 2 \* $$(nproc)`
	-make -C ${NODETOP} jstest CI_SKIP_TESTS="parallel/test-https-selfsigned-no-keycertsign-no-crash.js"
	mv ${NODEBUILD}/node ${NODEBUILD}/node.static
	./link-km.sh ${NODEBUILD} ${NODEOUT} ${OS_NAME}

clobber:
	rm -rf node

clean:
	rm -rf $(RUNENV_PATH)
	@echo "Note - 'clean' doesn't remove ./node artifacts. use 'clobber' to force remove build artifacts"

test test-short test-long test-all: ${PAYLOAD_KM} ${CCTEST_KM}
	scripts/test-run.sh $@ ${KM_BIN} ${KM_CLI_BIN} ${PAYLOAD_KM} ${CCTEST_KM} ${NODETOP} ${BUILD}

# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := scripts/test-run.sh test km km_cli ${PAYLOAD_KM} ${CCTEST_KM}

# TODO: revert when kkm is fixed
ifeq (${HYPERVISOR_DEVICE},/dev/kkm)
CONTAINER_TEST_ALL_CMD := scripts/test-run.sh test km km_cli ${PAYLOAD_KM} ${CCTEST_KM} ${NODETOP} ${BUILD}
else
CONTAINER_TEST_ALL_CMD := scripts/test-run.sh test-all km km_cli ${PAYLOAD_KM} ${CCTEST_KM} ${NODETOP} ${BUILD}
endif

# Use existing small dir for buildenv image builds (saves on sending data to docker svc)
BUILDENV_PATH  := ./scripts

# Param for 'make validate-runenv-image'. Used to do a quick validation of runtime environment.
RUNENV_VALIDATE_CMD := ("/scripts/hello.js")

RUNENV_TAG := node node-${VERS}
