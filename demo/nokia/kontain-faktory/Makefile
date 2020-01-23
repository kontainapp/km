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
# Places all Kontain file (in container) into TARGET_DIR
#

TOP := $(shell git rev-parse --show-cdup)

# Kontain files land here in the produced containers
TARGET_DIR ?= /opt/kontain/java/bin
# location of java bins and libs in KM repo, relative to TOP
JDK_LOC := payloads/java/jdk-11+28/build/linux-x86_64-server-release/jdk
# Kontain specific fails to land in the container. We also need 'env' since some old env's do not support '-S'
KM_FILES = ${KM_BIN} ${KM_LDSO} ${TOP}${JDK_LOC}/bin/java.kmd /usr/bin/env

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

${COMPONENTS}: ${KM_FILES} ${KDIR}/java ${DOCKERFILE} Makefile
	cp $(KM_FILES) ${KDIR}
	@# a hack - for shabang, we need .km not .kmd for now
	cp ${KDIR}/java.kmd ${KDIR}/java.km
	${DOCKER_BUILD} -q \
		--build-arg BASE=$(REPO)/$(notdir $(subst .,,$@)):latest \
		--build-arg TARGET_DIR="${TARGET_DIR}" \
		--build-arg KDIR="${KDIR}" \
		-t kontain/nokia-$(notdir $(subst .,,$@)) -f ${DOCKERFILE} .
	touch $@

${KDIR}: |
	mkdir -p $@

# 'java' is a shebang file calling KM with proper params. Use KMFLAGS="..." to generate container with proper flags
${KDIR}/java: ${KDIR} Makefile
	echo "#!${TARGET_DIR}/env -S ${TARGET_DIR}/$(notdir ${KM_BIN}) ${KMFLAGS} --dynlinker=${TARGET_DIR}/$(notdir ${KM_LDSO}) --copyenv" > $@
	chmod a+x $@

base: base-pull base-retag ## pull and retag nokia containers - prerequsite for 'make all'

base-retag: ## retag images pulled from cloud repo to 'latest' - needed by 'make all'
	docker tag $(REPO)/$(Z_VERSION) $(REPO)/zookeeper:latest
	docker tag $(REPO)/$(K_VERSION) $(REPO)/kafka:latest

base-pull: ## A helper to pull images from repository. Warning: tags are hardcoded
	@echo DO NOT FORGET TO: make login
	docker pull $(REPO)/$(Z_VERSION)
	docker pull $(REPO)/$(K_VERSION)

login: ## helper to log in Azure and the registry
	make -C $(TOP)cloud/azure login-cli
	az acr login -n kontainstage

clean: ## Clean temp files and containers
	rm -rf $(KDIR)
	-docker rmi `docker image ls -q "kontain/nokia-*"`

# This is a hack to get it going - we just mount stuff from the host. Will need to go to the container later
loc := $(realpath ${JDK_LOC}/lib)
test:  ## Sanity checking for the resulting kafka container
	# $(DOCKER_RUN) --device=/dev/kvm kontain/nokia-kafka
	$(DOCKER_RUN) -it --rm --device=/dev/kvm \
		 -e LD_LIBRARY_PATH=/opt/kontain/jlib/:/opt/kontain/jlib/server:/opt/kontain/lib64:/lib64 \
		 -v /opt/kontain/lib64/:/opt/kontain/lib64/:z  \
		 -v ${loc}:/opt/kontain/jlib:z \
		 -v ${loc}/server:/opt/kontain/jlib/server:z \
		kontain/nokia-kafka ${ARGS}

include ${TOP}make/locations.mk
