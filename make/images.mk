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
# Builds 3 type of images
#  - buildenv-image (environment for build)
#  - testenv-image (all artifacts for running build suites, including KM and payloads)
#  - runenv-image (minimal image for running KM+payload)
#
# The first 2 are explained in docs/build.md and docs/build-test-make-targets-and-images.md
#
# The runenv-image  is a bare bones image for specific payload.
# 		Can be built with 'make runenv-image' or 'make distro'. The following info
#		needs to be defined in Makefile for it to work
#		- BUILDENV_PATH is the location for Docker to use. Default is build/payloads/component
#		- runenv_prep function is (optional) code to copy stuff to the BUILDENV_PATH, or modify it
#				before running Docker. E.g. if BUILDENV_PATH=. , the runenv_prpe is likely not needed
#		- COMPONENT, PAYLOAD_NAME and PAYLOAD_KM also need to be defined.  See payloads/node
#		for examples
#

# make sure the value is non-empty and has trailing /
ifeq ($(strip ${TOP}),)
TOP := ./
endif

ifeq ($(COMPONENT),)
$(error "COMPONENT is undefined - please add COMPONENT=component_name in your Makefile")
endif
include ${TOP}make/locations.mk

# Image names and location for image builds
TEST_IMG := kontain/test-${COMPONENT}-${DTYPE}
BUILDENV_IMG := kontain/buildenv-${COMPONENT}-${DTYPE}
# runenv does not include anything linix distro specific, so it does not have 'DTYPE'
RUNENV_IMG := kontain/runenv-${COMPONENT}

# image names with proper registry
TEST_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(TEST_IMG))
BUILDENV_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(BUILDENV_IMG))
RUNENV_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(RUNENV_IMG))

TEST_DOCKERFILE ?= test-${DTYPE}.dockerfile
BUILDENV_DOCKERFILE ?= buildenv-${DTYPE}.dockerfile
RUNENV_DOCKERFILE ?= runenv.dockerfile

# Path 'docker build' uses for build, test and run environments
BUILDENV_PATH ?= .
TESTENV_PATH ?= .
RUNENV_PATH ?= ${BLDDIR}

testenv-image:  ## build test image with test tools and code
	@# Copy KM there. TODO - remove when we support pre-installed KM
	cp ${KM_BIN} ${TESTENV_PATH}
	cp ${KM_LDSO} ${TESTENV_PATH}
	${DOCKER_BUILD} --build-arg branch=${SRC_SHA} -t ${TEST_IMG}:${IMAGE_VERSION} ${TESTENV_PATH} -f ${TEST_DOCKERFILE}
	rm ${TESTENV_PATH}/$(notdir ${KM_BIN})

buildenv-image:  ## make build image based on ${DTYPE}
	${DOCKER_BUILD} -t ${BUILDENV_IMG}:${IMAGE_VERSION} ${BUILDENV_PATH} -f ${BUILDENV_DOCKERFILE}

push-testenv-image: testenv-image ## pushes image. Blocks ':latest' - we dont want to step on each other
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_test_image_version .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"  \
		FROM=$(TEST_IMG):$(IMAGE_VERSION) TO=$(TEST_IMG_REG):$(IMAGE_VERSION)

pull-testenv-image: ## pulls test image. Mainly need for CI
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
VARS_TO_PRINT ?= DIMG TEST_IMG BUILDENV_PATH TESTENV_PATH BUILDENV_IMG BUILDENV_DOCKERFILE KM_BIN DTYPE TEST_IMG_REG BUILDENV_IMG_REG SRC_BRANCH SRC_SHA

.PHONY: debugvars
debugvars:   ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))

# allows to do 'make print-varname'
print-%  : ; @echo $* = $($*)

# use this embedded dockerfile... we need it to replace ENTRYPOINT
export define DOCKERFILE_CONTENT
cat <<EOF
	FROM scratch
	LABEL Description="${PAYLOAD_NAME} in Kontain" Vendor="Kontain.app" Version="0.1"
	ADD . /
	ENTRYPOINT [ "/km", "$(notdir $(PAYLOAD_KM))" ]
EOF
endef

runenv-image: ${RUNENV_PATH}  ${KM_BIN}  ## Build minimal runtime image
	@$(TOP)make/check-docker.sh
	@-docker rmi -f ${RUNENV_IMG}:latest 2>/dev/null
	cp ${KM_BIN} ${RUNENV_PATH}
ifdef runenv_prep
	@echo -e "Executing prep steps"
	eval $(runenv_prep)
endif
	eval "$$DOCKERFILE_CONTENT"  | ${DOCKER_BUILD} -t ${RUNENV_IMG} -f - ${RUNENV_PATH}
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls ${RUNENV_IMG} --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"

validate-runenv-image: ## Validate runtime image
	${DOCKER_RUN_TEST} ${RUNENV_IMG} ${RUNENV_VALIDATE_CMD}

push-runenv-image: testenv-image ## pushes image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"  \
		FROM=$(RUNENV_IMG):$(IMAGE_VERSION) TO=$(RUNENV_IMG_REG):$(IMAGE_VERSION)

pull-runenv-image: ## pulls test image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"  \
		FROM=$(RUNENV_IMG_REG):$(IMAGE_VERSION) TO=$(RUNENV_IMG):$(IMAGE_VERSION)

distro: runenv-image ## an alias for runenv-image
publish: push-runenv-image

${BLDDIR}:
	mkdir -p ${BLDDIR}
