# Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

TOP_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
SRC_REPO_DIR := ${TOP_DIR}/rest-service-11
SRC_DIR := ${SRC_REPO_DIR}/complete
APP_JAR := ${SRC_DIR}/build/libs/rest-service-0.0.1-SNAPSHOT.jar

DEFAULT_DEMO_PORT := 8080

clean_container = @-docker rm --force ${1} 2>/dev/null
clean_container_image = @-docker rmi -f ${1} 2>/dev/null

.PHONY: all
all: | build build/demo

.PHONY: build
build: | ${SRC_REPO_DIR} ${APP_JAR}

REPO_COMMIT := f7f2acb31d27f57550d6a9d37bbe50e185d1ca4b
${SRC_REPO_DIR}:
	git clone https://github.com/spring-guides/gs-rest-service.git ${SRC_REPO_DIR}
	cd ${SRC_REPO_DIR}; git reset --hard ${REPO_COMMIT}

BUILDER_CONTAINER := spring-boot-demo-builder
${APP_JAR}:
	$(call clean_container,${BUILDER_CONTAINER})
	docker run --rm -it -d -v ${SRC_REPO_DIR}:/app:z --name ${BUILDER_CONTAINER} java
	docker exec -w /app/complete -u $(shell id -u):$(shell id -g) ${BUILDER_CONTAINER} ./gradlew build
	$(call clean_container,${BUILDER_CONTAINER})

DEMO_VERSION ?= 1.0
DEMO_IMAGE ?= kontainapp/spring-boot-demo:${DEMO_VERSION}
DEMO_CONTAINER ?= kontainapp_sprint_boot_demo

.PHONY: build/demo
# Docker requires relative path to the building context
build/demo: TARGET_JAR_PATH := $(shell realpath --relative-to="${TOP_DIR}" "${APP_JAR}")
build/demo: clean/image
	docker build -t ${DEMO_IMAGE} --build-arg TARGET_JAR_PATH=${TARGET_JAR_PATH} -f kontain.dockerfile ${TOP_DIR}

KONTAIN_ENGINE_VOLUME_MAPPING := -v /opt/kontain/bin:/opt/kontain/bin:z 
KONTAIN_ENGINE_VOLUME_MAPPING += -v /opt/kontain/runtime:/opt/kontain/runtime:z 
.PHONY: run/demo
run/demo:
	docker run --rm -d --device=/dev/kvm ${KONTAIN_ENGINE_VOLUME_MAPPING} -p ${DEFAULT_DEMO_PORT}:8080 --name ${DEMO_CONTAINER} ${DEMO_IMAGE}

.PHONY: clean/build
clean/build:
	$(call clean_container,${BUILDER_CONTAINER})
	rm -rf ${SRC_REPO_DIR}

.PHONY: clean/demo
clean/demo:
	$(call clean_container,${DEMO_CONTAINER})

.PHONY: clean/image
clean/image:
	$(call clean_container_image,${DEMO_IMAGE})

.PHONY: clean
clean: clean/build clean/demo clean/image

.PHONY: test
test: clean/test
	docker run --rm -d --device=/dev/kvm ${KONTAIN_ENGINE_VOLUME_MAPPING} -p ${DEFAULT_DEMO_PORT}:8080 --name ${DEMO_CONTAINER} ${DEMO_IMAGE}
	${TOP_DIR}/test/test.sh
	$(call clean_container,${DEMO_CONTAINER})

.PHONY: clean/test
clean/test:
	$(call clean_container,${DEMO_CONTAINER})
