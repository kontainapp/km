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
# Makefile for building and packing dweb-demo

# TODO: Needs to be resolved
# ifeq ($(ImageOS),ubuntu18)
# On Ubuntu18, ld refuses to deal with alpine:3.11.5  crt1.o with error
# '/usr/bin/ld: /opt/kontain/alpine-lib/usr/lib/crt1.o: unable to initialize decompress status for section .debug_info'
# googling shows it's a result of binutils/elfutils version mismatch.
# blocking dweb build on CI for now.
# .DEFAULT:
# 	@echo Building ${PAYLOAD_KM} on ${ImageOS} is not supported, ignoring target \'$@\'
# else

TOP := $(shell git rev-parse --show-toplevel)
COMPONENT := dweb
RUNENV_TAG := dweb

# this is how we run dweb.km tests in container
CONTAINER_TEST_CMD := ./dweb -x

PAYLOAD_KM := dweb/dweb.km
PAYLOAD_NAME := DWebSrvDemo
# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := ${PAYLOAD_KM} \
	dweb/css \
	dweb/fonts \
	dweb/js \
	dweb/index.html \
	dweb/kontain_logo_transparent.png \
	dweb/text.txt\
	dweb/basic.htm

# runenv-image
export define runenv_prep
	tar -cf - ${PAYLOAD_FILES} | tar -C $(RUNENV_PATH) -xf -
	mv $(RUNENV_PATH)/$(PAYLOAD_KM) $(basename $(RUNENV_PATH)/$(PAYLOAD_KM))
endef
RUNENV_VALIDATE_CMD := ("./dweb" "-x")
RUNENV_VALIDATE_EXPECTED := I am DWEB

include ${TOP}/make/images.mk

.PHONY: all build clean
all build:
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:${TOP}:z \
		${KM_OPT_VOLUME} \
		-w ${TOP}/payloads/demo-dweb \
		${BUILDENV_IMG_TAGGED} \
		make -C dweb $@

all: ## builds Linux and KM for payload in this folder
clean: ## Clean up the build
		make -C dweb $@

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
	-v ${KM_OPT_BIN}:${KM_OPT_BIN} \
	-p ${PORT1}:${PORT1} \
	${RUNENV_IMG_TAGGED} \
	${PORT1}

dockerclean: ## Clean up results of 'make dockerdemo' if it was killed with ^C
	-docker rm -f `docker ps -qa -f ancestor=${RUNENV_IMG_TAGGED}`

test-all: all ## Builds dweb all and runs simple test
	@echo -en "Testing dweb.km start/stop"
	@${KM_BIN} ${PAYLOAD_KM} -x | grep -q 'Exiting as requested' && echo ...passed

# endif
