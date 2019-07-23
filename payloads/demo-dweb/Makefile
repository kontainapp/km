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

TOP := $(shell git rev-parse --show-cdup)
KM_BIN=${TOP}/build/km/km


.PHONY: all build clean
all build clean:
	$(MAKE) -C dweb $@

all: ## builds Linux and KM for payload in this folder
clean: ## Clean up the build

PORT = 8080
PORT1 = 8081
demo: all ##  Run Simple web server demo as Linux process
	@echo use "http://localhost:${PORT}" to access the demo running as Linux process
	./dweb/dweb ${PORT} dweb

kmdemo:  all ## Run Simple web server demo as Kontain VM Payload
	@echo use "http://localhost:${PORT1}" to access the demo running as Linux process
	${TOP}build/km/km ./dweb/dweb.km ${PORT1} dweb

dockerdemo: all distro ## Run Simple web server demo as Kontain VM payload in Docker
	@echo use "http://localhost:${PORT1}" to access the demo running as a Kontain VM packaged in Docker
	@echo DO NOT FORGET TO run make dockerclean
	docker run --device /dev/kvm -t --rm -p ${PORT1}:${PORT1} ${PAYLOAD_IMAGE_NAME} \
		dweb.km ${PORT1}

dockerclean: ## CLean up results of 'make dockerdemo' if it was killed with ^C
	-docker rm -f `docker ps -qa -f label=Vendor=Kontain.app`

test-all: all ## Builds dweb all and runs simple test
	@echo -en "Testing dweb.km start/stop"; cd dweb; \
		../${KM_BIN} dweb.km -x | grep -q 'Exiting as requested' && echo ...passed

# Info for packaging into Docker Image

# KM file name, relative to currentdir.
PAYLOAD_KM := dweb/dweb.km
# image name, without repo and version tag
PAYLOAD_IMAGE_SHORT_NAME := dweb-km
# name to be added to label(s)
PAYLOAD_NAME := DWebSrvDemo
# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := $(PAYLOAD_KM) dweb/css dweb/fonts dweb/js dweb/index.html dweb/kontain_logo_transparent.png

include ${TOP}make/distro.mk
