# Copyright © 2018-2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Support for building docker images. Assumes locations.mk is included
#

# make sure the value is non-empty and has trailing /
ifeq ($(strip ${TOP}),)
TOP := ./
endif

ifeq ($(COMPONENT),)
$(error "COMPONENT is undefined - please add COMPONENT=component_name in your Makefile")
endif
include ${TOP}make/locations.mk

DOCKER_BUILD := docker build --label "KONTAIN:BRANCH=$(SRC_BRANCH)" --label "KONTAIN:SHA=$(SRC_SHA)"
DOCKER_RUN := docker run -t
DOCKER_RUN_TEST := docker run -t --rm --ulimit nofile=1024:1024 --device=/dev/kvm
# Use DOCKER_RUN_CLEANUP="" if container is needed after a run
DOCKER_RUN_CLEANUP ?= --rm
# DOCKER_RUN_FOR_TEST  := $(DOCKER_RUN) $(DOCKER_RUN_CLEANUP) --device=/dev/kvm --ulimit nofile=`ulimit -n`:`ulimit -n -H` -u${UID}:${GID}

# Image names and location for image builds
TEST_IMG := kontain/test-${COMPONENT}-${DTYPE}
BUILDENV_IMG := kontain/buildenv-${COMPONENT}-${DTYPE}

# image names with proper registry
TEST_IMG_REG := $(REGISTRY)/test-${COMPONENT}-${DTYPE}
BUILDENV_IMG_REG := $(REGISTRY)/buildenv-${COMPONENT}-${DTYPE}

TEST_DOCKERFILE ?= test-${DTYPE}.dockerfile
BUILDENV_DOCKERFILE ?= buildenv-${DTYPE}.dockerfile

# builednev image docker build location
BE_LOC ?= .
# Test image docker build location
TE_LOC ?= .
# Runtime env image docker build location
RE_LOC ?= $(TE_LOC)

test-image:  ## build test image with test tools and code
	@# Copy KM there. TODO - remove when we support pre-installed KM
	cp ${KM_BIN} ${TE_LOC}
	${DOCKER_BUILD} --build-arg branch=${SRC_SHA} -t ${TEST_IMG}:${IMAGE_VERSION} ${TE_LOC} -f ${TEST_DOCKERFILE}
	rm ${TE_LOC}/$(notdir ${KM_BIN})

buildenv-image:  ## make build image based on ${DTYPE}
	${DOCKER_BUILD} -t ${BUILDENV_IMG}:${IMAGE_VERSION} ${BE_LOC} -f ${BUILDENV_DOCKERFILE}

push-test-image: test-image ## pushes image. Blocks ':latest' - we dont want to step on each other
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_test_image_version .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"  \
		FROM=$(TEST_IMG):$(IMAGE_VERSION) TO=$(TEST_IMG_REG):$(IMAGE_VERSION)

pull-test-image: ## pulls test image. Mainly need for CI
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_test_image_version .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"  \
		FROM=$(TEST_IMG_REG):$(IMAGE_VERSION) TO=$(TEST_IMG):$(IMAGE_VERSION)

# a few of Helpers for push/pull image and re-tag
.check_test_image_version:
	@if [[ "$(IMAGE_VERSION)" == "latest" ]] ; then \
		echo -e "$(RED)PLEASE SET IMAGE_VERSION, 'latest' is not supported for test images$(NOCOLOR)" ; false; fi

.push-image:
	@if [ -z "$(FROM)" -o -z "$(TO)" ] ; then echo Missing params FROM or TO, exiting; false; fi
	docker tag $(FROM) $(TO)
	docker push $(TO)
	docker rmi $(TO)

.pull-image:
	@echo -e "$(CYAN)Do not forget to login to azure using 'make -C GIT_TOP/cloud/azure login'$(NOCOLOR)"
	@if [ -z "$(FROM)" -o -z "$(TO)" ] ; then echo Missing params FROM or TO, exiting; false; fi
	docker pull $(FROM)
	docker tag $(FROM) $(TO)
	docker rmi $(FROM)

# for this target to work, set FORCE_BUILDENV_PUSH to 'force'. Also set IMAGE_VERSION
# to the version you want to push. BE CAREFUL - it pushes to shared image !!!
push-buildenv-image: ## Pushes to buildnev image. PROTECTED TARGET
	@if [[ "$(FORCE_BUILDENV_PUSH)" != "force" ]] ; then \
		echo -e "$(RED)Unforced push of buildenv images is not supported$(NOCOLOR)" && false; fi
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		FROM=$(BUILDENV_IMG):$(IMAGE_VERSION) TO=$(BUILDENV_IMG_REG):$(IMAGE_VERSION)

pull-buildenv-image: ## Pulls the buildenv image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		FROM=$(BUILDENV_IMG_REG):$(IMAGE_VERSION) TO=$(BUILDENV_IMG):$(IMAGE_VERSION)

#
# 'Help' target - based on '##' comments in targets
#
# This target ("help") scans Makefile for '##' in targets and prints a summary
# Note - used awk to print (instead of echo) so escaping/coloring is platform independed
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
	/^[.a-zA-Z0-9_-]+:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo 'For specific help in folders, try "(cd <dir>; make help)"'
	@echo ""


# Support for simple debug print (make debugvars)
VARS_TO_PRINT ?= DIMG TEST_IMG BE_LOC TE_LOC BUILDENV_IMG BUILDENV_DOCKERFILE KM_BIN DTYPE TEST_IMG_REG BUILDENV_IMG_REG SRC_BRANCH SRC_SHA

.PHONY: debugvars
debugvars:   ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))

todo:  ## List of TODOs
	@echo -e "$(GREEN)"
	@echo '* replace distro.mk and distro targets with make run-image; test and change demo and .md'
	@echo '* add pull/push to github.com shared so no login is needed for buildenv'
	@echo '* write a small script and a target and clean up junk from registry'
	@echo -e "$(NOCOLOR)"

# For now, use vars that distro.mk assumed

test-distro:
	${DOCKER_RUN_TEST} ${RUNENV_IMG} ${RUNENV_TEST_PARAM}
# KM monitor path
KM_BIN=${TOP}/build/km/km
# KM file name, relative to currentdir. Note that the same will be absolute in the container
# PAYLOAD_KM := cpython/python.km
# name to be added to label(s)
PAYLOAD_NAME := Node.js-10

RUNENV_TEST_PARAM = /out/Release/hello.js

# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := --exclude='*/test/*' out/Release km

RUNENV_IMG := kontain/runenv-${COMPONENT}-${DTYPE}
RUNENV_IMG_REG := $(REGISTRY)/runenv-${COMPONENT}-${DTYPE}

# RE_LOC := ${TOP}/build/${COMPONENT_RUNENV}
RE_LOC := node

#
# TODO:
# - mkdir -p build/payloads/node/runenv
# - form payload.tar content, PIPE there
# - edit shebang file if any THERE
# - package docker image
# vars: PAYLOAD_FILES - list of stuff to tar up
#  runenv.dockerfile - dockerfile for constructing KM payload. Note IT IS NOT platform dependent. Produces runenv-node-km. LABEL keeps the original platform

distro runtime-image: ## (ALPHA) Build minimap runtime image
	@$(TOP)make/check-docker.sh
	# mkdir -p $(RE_LOC)
	cp ${KM_BIN} $(RE_LOC)
	cp scripts/* $(RE_LOC)/out/Release
	tar -C $(RE_LOC) -cf $(RE_LOC)/$(TAR_FILE) $(PAYLOAD_FILES)
	#-echo "Cleaning old image"; \
		docker rmi -f ${RUNENV_IMG}:latest 2>/dev/null
	echo -e "Building new image with generated Dockerfile:\\n $(DOCKERFILE_CONTENT)"
	echo -e $(DOCKERFILE_CONTENT) | docker build --force-rm \
		-t  kontain/runenv-${COMPONENT}-${DTYPE}:latest -f - ${RE_LOC}
	# docker tag ${PAYLOAD_LATEST}
	rm $(RE_LOC)/$(TAR_FILE) $(RE_LOC)/km
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls $(RUNENV_IMG) --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"


# Build KM payload container, using KM container as the base image
TAR_FILE := payload.tar
DOCKERFILE_CONTENT := \
	FROM scratch \\n \
	LABEL Description=\"${PAYLOAD_NAME} \(/${COMPONENT}\) in Kontain\" Vendor=\"Kontain.app\" Version=\"0.1\"	\\n \
	ADD km $(TAR_FILE) / \\n \
	WORKDIR /$(dir $(PAYLOAD_KM)) \\n \
	ENTRYPOINT [ \"/out/Release/node\"]

