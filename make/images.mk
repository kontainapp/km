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
# Use DOCKER_RUN_CLEANUP="" if container is needed after a run
DOCKER_RUN_CLEANUP ?= --rm
DOCKER_RUN_FOR_TEST  := $(DOCKER_RUN) $(DOCKER_RUN_CLEANUP) --device=/dev/kvm --ulimit nofile=`ulimit -n`:`ulimit -n -H` -u${UID}:${GID}

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
T_LOC ?= .

test-image:  ## build test image with test tools and code
	@# Copy KM there. TODO - remove when we support pre-installed KM
	cp ${KM_BIN} ${T_LOC}
	${DOCKER_BUILD} --build-arg branch=${SRC_SHA} -t ${TEST_IMG}:${IMAGE_VERSION} ${T_LOC} -f ${TEST_DOCKERFILE}
	rm ${T_LOC}/$(notdir ${KM_BIN})

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
VARS_TO_PRINT ?= DIMG TEST_IMG BE_LOC T_LOC BUILDENV_IMG BUILDENV_DOCKERFILE KM_BIN DTYPE TEST_IMG_REG BUILDENV_IMG_REG SRC_BRANCH SRC_SHA

.PHONY: debugvars
debugvars:   ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))

todo:  ## List of TODOs
	@echo -e "$(GREEN)"
	@echo '* validate make; make test ; make test-all. Also get a list of targetsand check them'
	@echo '* cleanup images in CD; review .md file to see what else is missing'
	@echo '* update and test CI -hack. THEN clean up seds to use kustomize if possible'
	@echo '* update .md files ; build.md etc.'
	@echo '* replace distro.mk and distro targets with make run-image; test and change demo and .md'
	@echo '* add pull/push to github.com shared so no login is needed for buildenv'
	@echo '* write a small script and a target and clean up junk from registry'
	@echo -e "$(NOCOLOR)"
