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
# Makefile for building and packing dweb-demo

TOP := $(shell git rev-parse --show-toplevel)
COMPONENT := dweb

PAYLOAD_KM := dweb/dweb.km
PAYLOAD_NAME := DWebSrvDemo
# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := ${PAYLOAD_KM} \
	dweb/css \
	dweb/fonts \
	dweb/js \
	dweb/index.html \
	dweb/kontain_logo_transparent.png \
	dweb/favicon.ico \
	dweb/text.txt\
	dweb/basic.htm

IMAGE_VERSION := latest

.PHONY: all build clean
all build clean:
	$(MAKE) -C dweb $@

all: ## builds Linux and KM for payload in this folder
clean: ## Clean up the build

PORT = 8080
PORT1 = 8081
demo: all ## Run Simple web server demo as Linux process
	@echo use "http://localhost:${PORT}" to access the demo running as Linux process
	./dweb/dweb ${PORT} dweb

kmdemo:  all ## Run Simple web server demo as Kontain VM Payload
	@echo use "http://localhost:${PORT1}" to access the demo running as Linux process
	${KM_BIN} ${PAYLOAD_KM} ${PORT1}

dockerdemo: all distro ## Run Simple web server demo as Kontain VM payload in Docker
	$(info use "http://localhost:${PORT1}" to access the demo running as a Kontain VM packaged in Docker)
	$(info DO NOT FORGET TO run make dockerclean)
	${DOCKER_RUN_TEST} \
	-v ${KM_BIN}:${KM_OPT_BIN} \
	-p ${PORT1}:${PORT1} \
	${RUNENV_IMG}:${IMAGE_VERSION} \
	${PORT1}

dockerclean: ## Clean up results of 'make dockerdemo' if it was killed with ^C
	-docker rm -f `docker ps -qa -f ancestor=${RUNENV_IMG}:${IMAGE_VERSION}`

test-all: all ## Builds dweb all and runs simple test
	@echo -en "Testing dweb.km start/stop"
	@${KM_BIN} ${PAYLOAD_KM} -x | grep -q 'Exiting as requested' && echo ...passed

# runenv-image
export define runenv_prep
	tar -cf - ${PAYLOAD_FILES} | tar -C $(RUNENV_PATH) -xf -
endef
RUNENV_VALIDATE_CMD := "-x"
RUNENV_VALIDATE_EXPECTED := I am DWEB

include ${TOP}/make/images.mk
