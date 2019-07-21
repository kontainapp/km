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
BUILD=Debug

# KM monitor path
KM_BIN=${TOP}/build/km/km
# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := node/out/$(BUILD)/node.km
# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := node-km
# name to be added to label(s)
PAYLOAD_NAME := NodeJs12.4

# a list to be passed to tar to be packed into container image - TODO !
PAYLOAD_FILES := --exclude='*/test/*' scripts $(PAYLOAD_KM)

all: $(PAYLOAD_KM)

$(PAYLOAD_KM): *sh *patch
	./build.sh

# force the build
build:
	./build.sh

link:
	./link-km.sh

# TODO: we need to modify and use 'make -j4 test' in Node, per 'pull-requests.md'
test-all:
	${KM_BIN} ${PAYLOAD_KM} ./scripts/hello.js
	@echo noop.js - expecting exit with code 22:
	${KM_BIN} ${PAYLOAD_KM} ./scripts/noop.js ; [ $$? -eq 22 ]
	${KM_BIN} ${PAYLOAD_KM} ./scripts/micro-srv.js & sleep 0.5 ; curl localhost:8080
	-curl -X POST localhost:8080 # force the srv to exit and ignore curl 'empty reply'

include ${TOP}make/distro.mk
