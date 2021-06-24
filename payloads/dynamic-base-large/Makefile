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

COMPONENT := dynamic-base-large

# name to be added to label(s)
PAYLOAD_NAME := Dynamic-base-large
PAYLOAD_FILES := -C /opt/kontain/ --exclude '*.a' runtime lib alpine-lib
# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := ./km hello_test.kmd Hello 'The One'
CONTAINER_TEST_ALL_CMD := ${CONTAINER_TEST_CMD}
TESTENV_EXTRA_FILES := ${TOP}/tests/hello_test.kmd

test test-krun test-all:
	@echo Skipping tests

clean clobber:
	@true

RUNENV_VALIDATE_CMD := ("hello_test" "Hello, World!")
RUNENV_VALIDATE_EXPECTED := Hello, World

export define runenv_prep
	tar ${PAYLOAD_FILES} -czf ${RUNENV_PATH}/libs.tar
	cp ${TOP}/tests/hello_test.kmd ${RUNENV_DEMO_PATH}
endef
include ${TOP}/make/images.mk
