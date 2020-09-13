# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

# Basic build for java faktory demo

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/locations.mk

SRC_REPO_DIR := ${CURDIR}/springboot/gs-rest-service
ORIGINAL_IMAGE_NAME := kontainapp/faktory-java-demo-original

all: original ## Default target (builds 'original')

.PHONY: original
original: ${SRC_REPO_DIR} ## Build the original container
	docker build -t ${ORIGINAL_IMAGE_NAME} springboot/

${SRC_REPO_DIR}: ## clone git repo for demo
	git clone -b '2.1.6.RELEASE' --depth 1 https://github.com/spring-guides/gs-rest-service.git ${SRC_REPO_DIR}

.PHONY: clean
clean: ## Clean up docker images
	-docker rmi ${ORIGINAL_IMAGE_NAME}
	rm -rf ${SRC_REPO_DIR}
