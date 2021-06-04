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
# Makefile for building and packing Python payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

COMPONENT := busybox
BUSYBOX_REPO := https://github.com/mirror/busybox.git
BUSYBOX_BRANCH ?= 1_33_1

BUSYBOX_TOP :=  ${TOP}/payloads/busybox/busybox

# KM monitor path
KM_BIN=${TOP}/build/km/km
PAYLOAD_KM := ${BUSYBOX_TOP}/_install/bin/busybox
# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := busybox-km
# name to be added to label(s)
PAYLOAD_NAME := Busybox
# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := busybox/_install run-test.sh run-all-tests.sh
# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := ./run-test.sh
CONTAINER_TEST_ALL_CMD := ./run-all-tests.sh

all build link: ${PAYLOAD_KM}

$(PAYLOAD_KM): ${BUSYBOX_TOP}/.config
	${DOCKER_RUN_BUILD} --env=PATH=${PATH} \
		-v ${TOP}:${TOP}:z \
		-v ${KM_OPT_RT}:${KM_OPT_RT}:Z \
		-v ${KM_OPT_BIN}:${KM_OPT_BIN}:Z \
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

RUNENV_VALIDATE_CMD := /bin/sh -c "echo Hello, World!"
RUNENV_VALIDATE_EXPECTED := Hello

export define runenv_prep
	tar -cf - ${PAYLOAD_FILES} | tar -C $(RUNENV_PATH) -xf -
endef
include ${TOP}/make/images.mk
