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

TOP := $(shell git rev-parse --show-cdup)

# KM monitor path
KM_BIN=${TOP}/build/km/km
# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := cpython/python.km
# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := python-km
# name to be added to label(s)
PAYLOAD_NAME := Python3.7

# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := --exclude='*/test/*' --exclude='*/__pycache__/*' --exclude '*.exe' --exclude '*.whl' \
	cpython/python.km cpython/Lib cpython/Modules/Setup scripts

all: $(PAYLOAD_KM)

$(PAYLOAD_KM): *sh *patch
	./build.sh

# force the build
build:
	./build.sh

link:
	./link-km.sh

TEST_CMD ?= ${KM_BIN} ${PAYLOAD_KM}
test-all:
	-${TEST_CMD} ./test_unittest.py
	${TEST_CMD} ./cpython/Lib/unittest/test/

include ${TOP}make/distro.mk
