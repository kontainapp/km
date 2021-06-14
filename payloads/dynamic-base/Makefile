# Copyright © 2021 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

TOP := $(shell git rev-parse --show-toplevel)

COMPONENT := dynamic-base

# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := dynamic-base
# name to be added to label(s)
PAYLOAD_NAME := Dynamic-base
PAYLOAD_FILES := -C /opt/kontain/ --exclude '*.a' runtime
# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := ./km hello_test.kmd Hello "The One"
CONTAINER_TEST_ALL_CMD := ${CONTAINER_TEST_CMD}
TESTENV_EXTRA_FILES := ${TOP}/tests/hello_test.kmd

all build link: ${PAYLOAD_KM}

test test-krun test-all:
	@echo Skipping tests

clean clobber:
	@true

RUNENV_VALIDATE_CMD := ("hello_test" "Hello, World!")
RUNENV_VALIDATE_EXPECTED := Hello, World

export define runenv_prep
	tar -C /opt/kontain/ --exclude '*.a' -cf - runtime | tar -C ${RUNENV_PATH} -xf -
	cp ${TOP}/tests/hello_test.kmd ${RUNENV_DEMO_PATH}
endef
include ${TOP}/make/images.mk
