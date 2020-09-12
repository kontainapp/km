TOP := $(shell git rev-parse --show-toplevel)

include ${TOP}/make/locations.mk

CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
SRC_REPO_DIR := ${CURRENT_DIR}/springboot/gs-rest-service
ORIGINAL_IMAGE_NAME := kontainapp/faktory-java-demo-original

all: original

.PHONY: original
original: ${SRC_REPO_DIR}
	docker build -t ${ORIGINAL_IMAGE_NAME} springboot/

${SRC_REPO_DIR}:
	git clone -b '2.1.6.RELEASE' --depth 1 https://github.com/spring-guides/gs-rest-service.git ${SRC_REPO_DIR}

.PHONY: clean
clean:
	-docker rmi ${ORIGINAL_IMAGE_NAME}
	rm -rf ${SRC_REPO_DIR}
