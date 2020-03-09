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

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/locations.mk

# instructions for building Kontainers
DOCKERFILE := kontain.dockerfile

# JDK version we use from Kontain repo
JDK_VERSION ?= jdk-11.0.6+10
# Kontain dependency libs live here
KONTAIN_DIR := /opt/kontain
# location of java bins and libs in KM repo, relative to TOP
JDK_LOC := payloads/java/${JDK_VERSION}/build/linux-x86_64-normal-server-release

# Kontain specific files to land in the container JAVA/bin
KM_FILES := ${KM_BIN} ${KM_LDSO} ${TOP}/${JDK_LOC}/images/jdk/bin/java.km

# Place for docker to pick up stuff from
DOCKER_DIR := .dockerdir
# packaged and stripped files (.so, km, etc...)
KM_JAVA_TAR := ${DOCKER_DIR}/km_java_files.tar

# Docker repo to take the images from
REPO := kontainstage.azurecr.io/nokia/ckaf
CLIENT_REPO := kontainstage.azurecr.io/nokia/atg
# prefix for target containers
KONTAIN_REPO := kontain/nokia
# source containers names in repo. Hardcoded per component, to simplify the Makefile
Z_VERSION := zookeeper:2.0.0-3.4.14-2696
K_VERSION := kafka:2.0.0-5.3.1-2696
CLIENT_VERSION := kafka-client:4.1.2-2

# JDK locations in the java containers we convert. We assume all images have the same java version
ORIG_JDK_DIR := $(shell docker run --rm ${REPO}/${Z_VERSION} \
					 bash -c 'dirname $$(dirname $$(realpath $${JAVA_HOME:-/usr}/bin/java))')
ORIG_JDK_BIN := ${ORIG_JDK_DIR}/bin

all = $(DOCKER_DIR)/.all
.PHONY: all
all: ${all} ## Build all

${all}: ${KM_JAVA_TAR} ${DOCKERFILE} Makefile
	@if [ ! -d ${KONTAIN_DIR}/runtime ] ; then \
		echo "Missing ${KONTAIN_DIR}/runtime. Please run 'make pull-buildenv-image buildenv-local-fedora' in km/tests" ; false; \
	fi
	@set -x ; for item in ${Z_VERSION} ${K_VERSION} ; do \
		tag=${KONTAIN_REPO}/$$item ; echo -e "${GREEN}Building $$tag${NOCOLOR}" ; \
		${DOCKER_BUILD} -q -t $$tag \
		--build-arg FROM_IMAGE=${REPO}/$$item \
		--build-arg KONTAIN_DIR="${KONTAIN_DIR}" \
		--build-arg ORIG_JDK_DIR="${ORIG_JDK_DIR}" \
		-f ${DOCKERFILE} ${DOCKER_DIR} ; \
	done
	@docker image ls '${KONTAIN_REPO}/*'
	@if ! docker network list | grep -q kafka-net ; then \
		echo -e "${CYAN}Warning: kafka-net is missing. Run 'docker network create kafka-net'${NOCOLOR}"; fi
	@if [ -z "$$(rpm -qa sysstat)" ] ; then \
		echo -e "${CYAN}Warning: sysstat is missing. Run 'sudo dnf install sysstat -y'${NOCOLOR}"  ; fi
	@touch $@

# Copy all files and create 'java' file as a shebang calling KM with proper params.
# Use 'make KMFLAGS="..."' to generate container with proper flags passed to KM.
${KM_JAVA_TAR} : $(KM_FILES) ${KONTAIN_DIR} Makefile
	@if [ ! -d ${DOCKER_DIR} ] ; then mkdir -p ${DOCKER_DIR} ; fi
	@echo Copying ${KONTAIN_DIR}/lib64 ;\
		tar -C ${KONTAIN_DIR} -cf - lib64 runtime | tar -C ${DOCKER_DIR} -xf -
	@# We need 'env' since some old env's (e.g. one in centos7-minimal) do not support '-S'
	@echo Copying env...; \
		cp $(shell which env) ${DOCKER_DIR}
	@echo Copying KM files ;\
		bin=${DOCKER_DIR}/${JDK_VERSION}/bin; mkdir -p $$bin && cp --preserve=all $(KM_FILES) $$bin
	@echo Copying libs and release info from ${jdk_image_dir} ; \
		cp -rf --preserve=all ${jdk_image_dir}/{lib,release} ${DOCKER_DIR}/${JDK_VERSION}
	@echo -e "${GREEN}Stripping Kontain Java .so files...${NOCOLOR}" ; \
		find ${DOCKER_DIR}/${JDK_VERSION}/lib -name '*.so' -size +10k | xargs strip
	@echo Creating java shebang file...; \
		shebang=${DOCKER_DIR}/${JDK_VERSION}/bin/java; \
		echo "#!/usr/bin/env -S ${ORIG_JDK_BIN}/$(notdir ${KM_BIN}) ${KMFLAGS} --dynlinker=${ORIG_JDK_BIN}/$(notdir ${KM_LDSO}) --copyenv" > $$shebang ; chmod a+x $$shebang
	@echo Packaging Kontain and java files for Docker build...;\
		tar -C ${DOCKER_DIR}/${JDK_VERSION} -cf ${KM_JAVA_TAR} . ; rm -rf ${DOCKER_DIR}/${JDK_VERSION}

flatten: all ## Flatten images with buildah tool. Results will have prefix 'flat-'
	@if [ -z "$$(rpm -qa buildah)" ] ; then \
		echo -e "${CYAN}Warning: buildah is missing. Run 'sudo dnf install buildah -y'${NOCOLOR}"  ; false; \
	fi
	@echo -e "${GREEN}Flattening images with buildah tool${NOCOLOR}" ; \
	set -x; for item in ${Z_VERSION} ${K_VERSION} ; do \
		tag=${KONTAIN_REPO}/$$item ; \
		container=$$(buildah from docker-daemon:$$tag) ; \
		buildah commit --squash $$container docker-daemon:flat-${KONTAIN_REPO}/$$item ; \
		buildah rm $$container ; \
	done
	@docker image ls '*${KONTAIN_REPO}/*'

base pull: base-pull  ## aliases for base-pull

base-pull: ## pull nokia containers from azure container registry - prerequsite for 'make all'
	@echo DO NOT FORGET TO: make login
	docker pull $(REPO)/$(Z_VERSION)
	docker pull $(REPO)/$(K_VERSION)
	docker pull $(CLIENT_REPO)/$(CLIENT_VERSION)

login: ## helper to log in Azure and the registry
	make -C $(TOP)/cloud/azure login-cli
	az acr login -n kontainstage

clean-disks: ## clean disks and containers for Nokia Kafka test run
	-sudo rm -r $$(dirname ${BLDDIR})/disks
	-docker rm -f kafka-test-client zookeeper-server kafka-broker-1 kafka-broker-3 kafka-broker-3

clean: ## Clean temp files and containers
	sudo rm -rf $(DOCKER_DIR) $(COMPONENTS) ${test_data_dir}/zookeeper
	id=$$(docker ps -q --filter=name=${DOCKER_NAME}); if [ ! -z "$$id" ] ; then docker rm -f ${DOCKER_NAME} ; fi
	ids=$$(docker image ls -q "${KONTAIN_REPO}/*") ; if [ ! -z "$$ids" ] ; then \
			echo docker rmi $$ids; docker rmi $$ids; fi
	@echo -e "${GREEN}Done${NOCOLOR}"

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

# a helper to run truly interactive if needed - i.e. 'make test DOCKER_TI=-ti ARGS=bash'
DOCKER_TI ?= -t
# kontainer name, so we can delete it by name
DOCKER_NAME=kontain_nokia_test
test: all ## Sanity checking for the zookeeper Kontainer built with this make
	mkdir -p ${test_data_dir}/zookeeper/log ${test_data_dir}/zookeeper/data
	@id=$$(docker ps -q --filter=name=${DOCKER_NAME}); if [ ! -z "$$id" ] ; then docker rm -f ${DOCKER_NAME} ; fi
	docker run ${DOCKER_TI} --rm --device=/dev/kvm --name ${DOCKER_NAME} --network=kafka-net\
		${TEST_SCRIPT_ENV} \
		${KONTAIN_REPO}/${Z_VERSION} ${ARGS}

# we need .km for shebang, so if we have only .kmd, simply copy it
%.km: %.kmd
	cp $^ $@