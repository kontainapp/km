#
#  Copyright © 2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Automation for building KM-based containers for Nokia . 'make help' for help
#
# Places all Kontain files (in container) into KONTAIN_JAVA_BIN
#

TOP := $(shell git rev-parse --show-cdup)

# JDK version we are dealing with
JDK_VERSION ?= jdk-11+28

# Kontain files land here in the produced containers
KONTAIN_DIR := /opt/kontain
KONTAIN_JAVA_DIR := ${KONTAIN_DIR}/${JDK_VERSION}
KONTAIN_JAVA_BIN := ${KONTAIN_JAVA_DIR}/bin

# location for kontain files in the build tree
# all files from it will be placed to KONTAIN_DIR in the produced image
DOCKER_DIR := .dockerdir

# location of java bins and libs in KM repo, relative to TOP
JDK_LOC = payloads/java/${JDK_VERSION}/build/linux-x86_64-server-release

# Kontain specific fails to land in the container JDK bin.
# We also need 'env' since some old env's do not support '-S'
KM_FILES = ${KM_BIN} ${KM_LDSO} ${TOP}${JDK_LOC}/images/jdk/bin/java.kmd /usr/bin/env

# Targets to build locally before transferring data to dockerfile
JAVA_SHEBANG = ${DOCKER_DIR}/${JDK_VERSION}/bin/java
KONTAIN_LIBS = ${DOCKER_DIR}/lib64

# Docker repo to take the images from
REPO := kontainstage.azurecr.io/nokia/ckaf
# prefix for target containers
KONTAIN_REPO = kontain/nokia
# source containers names in repo. Hardcoded per component, to simplify the Makefile
Z_VERSION := zookeeper:2.0.0-3.4.14-2696
K_VERSION := kafka:2.0.0-5.3.1-2696

DOCKERFILE := kontain.dockerfile

all = $(DOCKER_DIR)/.all
.PHONY: all
all: ${all} ## Build all

${all}: ${KONTAIN_LIBS} ${JAVA_SHEBANG} ${DOCKERFILE} Makefile
	@for item in ${Z_VERSION} ${K_VERSION} ; do \
		tag=${KONTAIN_REPO}/$$item ; echo Building $$tag ; \
		${DOCKER_BUILD} -q -f ${DOCKERFILE} ${DOCKER_DIR} \
		--build-arg FROM_IMAGE=$(REPO)/$$item \
		--build-arg KONTAIN_DIR="${KONTAIN_DIR}" \
		--build-arg JDK_VERSION="${JDK_VERSION}" \
		 -t ${KONTAIN_REPO}/$$item ; \
	done
	@docker image ls '${KONTAIN_REPO}/*'
	@touch $@

# Copy all files and create 'java' file as a shebang calling KM with proper params.
# Use 'make KMFLAGS="..."' to generate container with proper flags passed to KM.
${JAVA_SHEBANG}: ${KM_FILES} Makefile
	@echo Copying KM files; \
		bin=${DOCKER_DIR}/${JDK_VERSION}/bin; mkdir -p $$bin && cp --preserve=all $(KM_FILES) $$bin
	@echo Copying libs and conf from ${jdk_image_dir} ; \
		cp -rf --preserve=all ${jdk_image_dir}/{lib,conf} ${DOCKER_DIR}/${JDK_VERSION}
	@find ${DOCKER_DIR}/${JDK_VERSION}/lib -name '*.so' -size +10k | xargs strip
	@echo Creating $@ ; \
		echo "#!${KONTAIN_JAVA_BIN}/env -S ${KONTAIN_JAVA_BIN}/$(notdir ${KM_BIN}) ${KMFLAGS} --dynlinker=${KONTAIN_JAVA_BIN}/$(notdir ${KM_LDSO}) --copyenv" > $@
	@cp $@.kmd $@.km # hack to allow KM to see java.km , not java.kmd
	@chmod a+x $@

${KONTAIN_LIBS}: |
	@echo Copying Kontain libs; mkdir -p $@; tar -C /opt/kontain -cf - lib64 | tar -C ${DOCKER_DIR} -xf -

base: base-pull base-retag ## pull and retag nokia containers - prerequsite for 'make all'

base-retag: ## retag images pulled from cloud repo to 'latest' - needed by 'make all'
	docker tag $(REPO)/$(Z_VERSION) $(REPO)/zookeeper:latest
	docker tag $(REPO)/$(K_VERSION) $(REPO)/kafka:latest

base-pull: ## A helper to pull images from repository.
	@echo DO NOT FORGET TO: make login
	docker pull $(REPO)/$(Z_VERSION)
	docker pull $(REPO)/$(K_VERSION)

login: ## helper to log in Azure and the registry
	make -C $(TOP)cloud/azure login-cli
	az acr login -n kontainstage

clean: ## Clean temp files and containers
	@rm -rf $(DOCKER_DIR) $(COMPONENTS) ${test_data_dir}/zookeeper
	@id=$$(docker ps -q --filter=name=${DOCKER_NAME}); if [ ! -z "$$id" ] ; then docker rm -f ${DOCKER_NAME} ; fi
	@ids=$$(docker image ls -q "${KONTAIN_REPO}/*") ; if [ ! -z "$$ids" ] ; then \
			echo docker rmi $$ids; docker rmi $$ids; fi
	@echo Done

# For tests, volume mount java .so libs from location below. This is a hack to get it going
jdk_image_dir := $(realpath ${TOP}/${JDK_LOC}/images/jdk)
# zookeper logs and data will go here
test_data_dir=/tmp

# this test reproduces environment used by createTestEnv.sh
TEST_SCRIPT_ENV := \
	-p 2181:2181 -e ZOOKEEPER_CLIENT_PORT=2181 \
	-e ZOOKEEPER_TICK_TIME=2000 \
	-e ZOOKEEPER_SYNC_LIMIT=2 \
	-e IS_RESTORE=false \
	-v ${test_data_dir}/zookeeper/log:/var/lib/zookeeper/log:z \
	-v ${test_data_dir}/zookeeper/data:/var/lib/zookeeper/data:z \
	--ulimit nofile=122880:122880

# not used yet. we will use it to locate and clean up old .so files.
java_loc=/etc/alternatives/jre_openjdk/lib

# a helper to run truly interactive if needed - i.e. 'make test DOCKER_TI=-ti ARGS=bash'
DOCKER_TI ?= -t
# kontainer name, so we can delete it by name
DOCKER_NAME=kontain_nokia_test
test: all ## Sanity checking for the zookeeper Kontainer built with this make
	mkdir -p ${test_data_dir}/zookeeper/log ${test_data_dir}/zookeeper/data
	@id=$$(docker ps -q --filter=name=${DOCKER_NAME}); if [ ! -z "$$id" ] ; then docker rm -f ${DOCKER_NAME} ; fi
	docker run ${DOCKER_TI} --rm --device=/dev/kvm --name ${DOCKER_NAME} \
		${TEST_SCRIPT_ENV} \
		${KONTAIN_REPO}/${Z_VERSION} ${ARGS}

include ${TOP}make/locations.mk
