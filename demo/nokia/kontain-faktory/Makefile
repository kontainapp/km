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

# Kontain files land here in the produced containers
KONTAIN_JAVA_DIR ?= /opt/kontain/java
KONTAIN_JAVA_BIN := ${KONTAIN_JAVA_DIR}/bin
# location of java bins and libs in KM repo, relative to TOP
JDK_LOC := payloads/java/jdk-11+28/build/linux-x86_64-server-release
# Kontain specific fails to land in the container. We also need 'env' since some old env's do not support '-S'
KM_FILES = ${KM_BIN} ${KM_LDSO} ${TOP}${JDK_LOC}/images/jdk/bin/java.kmd /usr/bin/env

# Docker repo to take the images from
REPO := kontainstage.azurecr.io/nokia/ckaf
# source containers names in repo. Hardcoded per component, to simplify the Makefile
Z_VERSION := zookeeper:2.0.0-3.4.14-2696
K_VERSION := kafka:2.0.0-5.3.1-2696

# source components flag files (i.e. KDIR/.component). Used to indicate the build for component is done
COMPONENTS = $(addprefix ${KDIR}/.,kafka zookeeper)

# temp location for kontain files for docker build
KDIR := kfilesdir
DOCKERFILE := kontain.dockerfile

all: ${COMPONENTS} ## Build all

${COMPONENTS}: ${KDIR}/java ${DOCKERFILE}
	${DOCKER_BUILD} -q -f ${DOCKERFILE} . \
		--build-arg FROM_IMAGE=$(REPO)/$(notdir $(subst .,,$@)):latest \
		--build-arg KONTAIN_JAVA_DIR="${KONTAIN_JAVA_DIR}" \
		--build-arg KDIR="${KDIR}" \
		 -t kontain/nokia-$(notdir $(subst .,,$@))
	touch $@

# Copy all files and create 'java' file as a shebang calling KM with proper params.
# Use 'make KMFLAGS="..."' to generate container with proper flags passed to KM.
${KDIR}/java: ${KM_FILES} Makefile
	mkdir -p ${KDIR} && cp --preserve=all $(KM_FILES) ${KDIR}
	echo "#!${KONTAIN_JAVA_BIN}/env -S ${KONTAIN_JAVA_BIN}/$(notdir ${KM_BIN}) ${KMFLAGS} --dynlinker=${KONTAIN_JAVA_BIN}/$(notdir ${KM_LDSO}) --copyenv" > $@
	chmod a+x $@
	@# a hack - for shabang, we need .km not .kmd for now
	cp ${KDIR}/java.kmd ${KDIR}/java.km

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
	@rm -rfv $(KDIR)
	@id=$$(docker ps -q --filter=name=${DOCKER_NAME}); if [ ! -z "$$id" ] ; then docker rm -f ${DOCKER_NAME} ; fi
	@ids=$$(docker image ls -q "kontain/nokia-*") ; if [ ! -z "$$ids" ] ; then \
			echo docker rmi $$ids; docker rmi $$ids; fi
	@echo Done

# For tests, volume mount java .so libs from location below. This is a hack to get it going
jdk_image_dir := $(realpath ${TOP}/${JDK_LOC}/images/jdk)
# zookeper logs and data will go here
data_dir=/tmp

# this test reproduces environment used by createTestEnv.sh, and adds LD_LIBRARY_PATH
# and related mounts. These latter things will go away - see TODO in kontain.dockerfile
TEST_SCRIPT_ENV := \
	-p 2181:2181 -e ZOOKEEPER_CLIENT_PORT=2181 \
	-e ZOOKEEPER_TICK_TIME=2000 \
	-e ZOOKEEPER_SYNC_LIMIT=2 \
	-e IS_RESTORE=false \
	-v ${data_dir}/zookeeper/log:/var/lib/zookeeper/log:z \
	-v ${data_dir}/zookeeper/data:/var/lib/zookeeper/data:z \
	--ulimit nofile=122880:122880

# not used yet. we will use it to locate and clean up old .so files.
java_loc=/etc/alternatives/jre_openjdk/lib

# a helper to run truly interactive if needed - i.e. 'make test DOCKER_TI=-ti ARGS=bash'
DOCKER_TI ?= -t
# kontainer name, so we can delete it by name
DOCKER_NAME=kontain_nokia_test
test:  ## Sanity checking for the zookeeper Kontainer built with this make
	mkdir -p ${data_dir}/zookeeper/log ${data_dir}/zookeeper/data
	@id=$$(docker ps -q --filter=name=${DOCKER_NAME}); if [ ! -z "$$id" ] ; then docker rm -f ${DOCKER_NAME} ; fi
	docker run ${DOCKER_TI} --rm --device=/dev/kvm --name ${DOCKER_NAME} \
			${TEST_SCRIPT_ENV} \
			-e LD_LIBRARY_PATH=/opt/kontain/jdk/lib/server:/opt/kontain/jdk/lib:/opt/kontain/lib64:/lib64 \
			-v /opt/kontain/lib64/:/opt/kontain/lib64/:z  \
			-v ${jdk_image_dir}:/opt/kontain/jdk:z \
		kontain/nokia-zookeeper ${ARGS}

include ${TOP}make/locations.mk
