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
# Makefile for building and packing Python payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

COMPONENT := busybox
BUSYBOX_REPO := https://github.com/mirror/busybox.git
BUSYBOX_BRANCH ?= 1_33_1

BUSYBOX_TOP :=  ${TOP}/payloads/busybox/busybox

PAYLOAD_KM := ${BUSYBOX_TOP}/_install/bin/busybox
# name to be added to label(s)
PAYLOAD_NAME := Busybox
# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := tmp -C busybox/_install .
# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := ./run-test.sh
CONTAINER_TEST_ALL_CMD := ./run-all-tests.sh

export define runenv_prep
	mkdir -p tmp && rm -rf tmp/* && chmod a+rwx tmp
	tar -cf $(RUNENV_PATH)/bin.tar ${PAYLOAD_FILES}
	cp ${CONTAINER_TEST_CMD} ${CONTAINER_TEST_ALL_CMD} ${RUNENV_PATH}
	mkdir -p ${RUNENV_PATH}/.kontain
endef

include ${TOP}/make/images.mk

all build link: ${PAYLOAD_KM}

$(PAYLOAD_KM): ${BUSYBOX_TOP}/.config
	${DOCKER_RUN_BUILD} --env=PATH=${PATH} \
		-v ${TOP}:${TOP}:z \
		${KM_OPT_VOLUME} \
		-w ${TOP}/payloads/busybox \
		${BUILDENV_IMG_TAGGED} bash -c \
		"make -C ${BUSYBOX_TOP} silentoldconfig && \
		make -C ${BUSYBOX_TOP} -j`expr 2 \* $$(nproc)` all install"

${BUSYBOX_TOP}: |
	git clone ${BUSYBOX_REPO} -b ${BUSYBOX_BRANCH}

${BUSYBOX_TOP}/.config: config.txt ${BUSYBOX_TOP}
	cp config.txt ${BUSYBOX_TOP}/.config

test:
	@${KM_BIN} ${PAYLOAD_KM} echo Basic Test passed

# expects krun is installed
test-krun:
	${DOCKER_RUN} -t --runtime=krun ${RUNENV_IMG_TAGGED} ls -l /bin | grep  'ls -> busybox'

test-all: test
	@${KM_BIN} ${PAYLOAD_KM} ls -l

clean clobber:
	rm -rf busybox

RUNENV_VALIDATE_CMD := ("/bin/sh" "-c" "echo Hello world!")
RUNENV_VALIDATE_EXPECTED := Hello world

RUNENV_TAG := ${COMPONENT}
