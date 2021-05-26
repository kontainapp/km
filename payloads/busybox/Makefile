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

PATH := $(realpath ${TOP}/tools):${PATH}

# KM monitor path
KM_BIN=${TOP}/build/km/km
# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := busybox-km
# name to be added to label(s)
PAYLOAD_NAME := Busybox
# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := busybox/_install
# component id for docker images
COMPONENT := busybox
# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := /km /bin/sh -c "echo Hello, World!"
CONTAINER_TEST_ALL_CMD := ${CONTAINER_TEST_CMD} && /bin/sh -c "ls -l"

all: $(PAYLOAD_KM)

$(PAYLOAD_KM) build link: *sh .config
	./build.sh

test-all:
	@${KM_BIN} ${PAYLOAD_KM} echo Basic Test passed
	@echo More TBD

clean:
	rm -rf busybox

export define runenv_prep
	tar -cf - ${PAYLOAD_FILES} | tar -C $(RUNENV_PATH) -xf -
endef
include ${TOP}/make/images.mk
