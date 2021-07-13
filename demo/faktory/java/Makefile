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
