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
# Support for building docker images.
#
# Builds 3 type of images
#  - buildenv-image (environment for build)
#  - testenv-image (all artifacts for running build suites, including KM and payloads)
#  - runenv-image (minimal image for running KM+payload)
#
# The first 2 are explained in docs/build.md and docs/images-targets.md
#
# The runenv-image is a bare bones image for specific payload.
# 		Can be built with 'make runenv-image'. The following info needs to be defined in Makefile for it to work
#		- BUILDENV_PATH is the location for Docker to use. Default is build/payloads/component
#		- runenv_prep function is (optional) code to copy stuff to the BUILDENV_PATH, or modify it
#				before running Docker. E.g. if BUILDENV_PATH=. , the runenv_prpe is likely not needed
#		- COMPONENT, PAYLOAD_NAME and PAYLOAD_KM also need to be defined. See payloads/node
#		for examples
#

ifeq ($(COMPONENT),)
$(error "COMPONENT is undefined - please add COMPONENT=component_name in your Makefile")
endif

include ${TOP}/make/locations.mk

# Image names and location for image builds
TEST_IMG := kontain/test-${COMPONENT}-${DTYPE}
BUILDENV_IMG := kontain/buildenv-${COMPONENT}-${DTYPE}
# runenv does not include anything linix distro specific, so it does not have 'DTYPE'
RUNENV_IMG := kontain/runenv-${COMPONENT}
# runenv demo image produce a demo based on the runenv image
RUNENV_DEMO_IMG := kontain/demo-runenv-${COMPONENT}

# image names with proper registry
TEST_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(TEST_IMG))
BUILDENV_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(BUILDENV_IMG))

RUNENV_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(RUNENV_IMG))
RUNENV_DEMO_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(RUNENV_DEMO_IMG))

TEST_DOCKERFILE ?= test-${DTYPE}.dockerfile
BUILDENV_DOCKERFILE ?= buildenv-${DTYPE}.dockerfile
RUNENV_DOCKERFILE ?= runenv.dockerfile
DEMO_RUNENV_DOCKERFILE ?= demo-runenv.dockerfile

# Path 'docker build' uses for build, test and run environments
BUILDENV_PATH ?= .
TESTENV_PATH ?= .
RUNENV_PATH ?= ${BLDDIR}
RUNENV_DEMO_PATH ?= .

TESTENV_EXTRA_FILES ?= ${KM_BIN} ${KM_LDSO}
testenv_prep = cp ${TESTENV_EXTRA_FILES} ${TESTENV_PATH}
testenv_cleanup = rm $(addprefix ${TESTENV_PATH}/,${notdir ${TESTENV_EXTRA_FILES}})

testenv-image: ## build test image with test tools and code
	$(call clean_container_image,${TEST_IMG}:${IMAGE_VERSION})
	$(call testenv_prep)
	${DOCKER_BUILD} --no-cache \
			--build-arg=branch=${SRC_SHA} \
			--build-arg=BUILDENV_IMAGE_VERSION=${BUILDENV_IMAGE_VERSION} \
			--build-arg=MODE=${BUILD} \
			-t ${TEST_IMG}:${IMAGE_VERSION} \
			-f ${TEST_DOCKERFILE} \
			${TESTENV_PATH}
	$(call testenv_cleanup)

buildenv-image: ${BLDDIR} ## make build image based on ${DTYPE}
	${DOCKER_BUILD} -t ${BUILDENV_IMG}:${BUILDENV_IMAGE_VERSION} \
		--build-arg=BUILDENV_IMAGE_VERSION=${BUILDENV_IMAGE_VERSION} \
		${BUILDENV_PATH} -f ${BUILDENV_DOCKERFILE}

push-testenv-image: testenv-image ## pushes image. Blocks ':latest' - we dont want to step on each other
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_test_image_version .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(TEST_IMG):$(IMAGE_VERSION) TO=$(TEST_IMG_REG):$(IMAGE_VERSION)

pull-testenv-image: ## pulls test image. Mainly need for CI
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_test_image_version .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
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
	@echo -e "Pulled image ${GREEN}${TO}${NOCOLOR}"

# Build env image push. For this target to work, set FORCE_BUILDENV_PUSH to 'force'. Also set IMAGE_VERSION
# to the version you want to push. BE CAREFUL - it pushes to shared image !!!
push-buildenv-image: ## Pushes to buildnev image. PROTECTED TARGET
	@if [[ "${BUILDENV_IMAGE_VERSION}" == "latest" && "$(FORCE_BUILDENV_PUSH)" != "force" ]] ; then \
		echo -e "$(RED)Unforced push of 'latest' buildenv images is not supported without setting FORCE_BUILDENV_PUSH to force$(NOCOLOR)" && false; fi
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		FROM=$(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION) TO=$(BUILDENV_IMG_REG):$(BUILDENV_IMAGE_VERSION)

pull-buildenv-image: ## Pulls the buildenv image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		FROM=$(BUILDENV_IMG_REG):$(BUILDENV_IMAGE_VERSION) TO=$(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION)

NO_RUNENV ?= false
ifeq (${NO_RUNENV}, false)

runenv-image: ${RUNENV_PATH} ${KM_BIN} ## Build minimal runtime image
	@$(TOP)/make/check-docker.sh
	$(call clean_container_image,${RUNENV_IMG}:${IMAGE_VERSION})
ifdef runenv_prep
	@echo -e "Executing prep steps"
	eval $(runenv_prep)
endif
	${DOCKER_BUILD} -t ${RUNENV_IMG}:${IMAGE_VERSION} -f ${RUNENV_DOCKERFILE} ${RUNENV_PATH}
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls ${RUNENV_IMG} --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"

ifneq (${RUNENV_VALIDATE_DIR},)
SCRIPT_MOUNT := -v $(realpath ${RUNENV_VALIDATE_DIR}):/$(notdir ${RUNENV_VALIDATE_DIR}):z
endif
RUNENV_VALIDATE_CMD ?= PlaceValidateCommandHere
RUNENV_VALIDATE_EXPECTED ?= Hello

# We use km from ${KM_BIN} here from the build tree instead of what's on the host under ${KM_OPT_BIN}.
validate-runenv-image: $(RUNENV_VALIDATE_DEPENDENCIES) ## Validate runtime image
	${DOCKER_RUN_TEST} \
		${KM_DOCKER_VOLUME} \
		${SCRIPT_MOUNT} \
		${RUNENV_IMG}:${IMAGE_VERSION} \
		${RUNENV_VALIDATE_CMD} | grep "${RUNENV_VALIDATE_EXPECTED}"

push-runenv-image:  runenv-image ## pushes image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(RUNENV_IMG):$(IMAGE_VERSION) TO=$(RUNENV_IMG_REG):$(IMAGE_VERSION)

pull-runenv-image: ## pulls test image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(RUNENV_IMG_REG):$(IMAGE_VERSION) TO=$(RUNENV_IMG):$(IMAGE_VERSION)

distro: runenv-image ## an alias for runenv-image
publish: push-runenv-image

demo-runenv-image: ${RUNENV_DEMO_DEPENDENCIES}
ifeq ($(shell test -e ${DEMO_RUNENV_DOCKERFILE} && echo -n yes),yes)
	$(call clean_container_image,${RUNENV_DEMO_IMG}:${IMAGE_VERSION})
	${DOCKER_BUILD} \
		-t ${RUNENV_DEMO_IMG}:${IMAGE_VERSION} \
		--build-arg runenv_image_version=${IMAGE_VERSION} \
		-f ${DEMO_RUNENV_DOCKERFILE} \
		${RUNENV_DEMO_PATH}
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls ${RUNENV_DEMO_IMG} --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"
else
	@echo -e "No demo dockerfile ${DEMO_RUNENV_DOCKERFILE} define. Skipping..."
endif

push-demo-runenv-image: demo-runenv-image
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(RUNENV_DEMO_IMG):$(IMAGE_VERSION) TO=$(RUNENV_DEMO_IMG_REG):$(IMAGE_VERSION)

endif # ifeq (${NO_RUNENV},)

# Default to something that gurantee to fail.
CONTAINER_TEST_CMD ?= \
	echo -e "${RED}CONTAINER_TEST_CMD needs to be defined to use this target${NOCOLOR}"; \
	false;

CONTAINER_TEST_ALL_CMD ?= ${CONTAINER_TEST_CMD}

test-withdocker: ## Run tests in local Docker. IMAGE_VERSION (i.e. tag) needs to be passed in
	${DOCKER_RUN_TEST} ${TEST_IMG}:${IMAGE_VERSION} ${CONTAINER_TEST_CMD}

test-all-withdocker: ## a special helper to run more node.km tests.
	${DOCKER_RUN_TEST} ${TEST_IMG}:${IMAGE_VERSION} ${CONTAINER_TEST_ALL_CMD}

# Helper when we need to make sure IMAGE_VERSION is defined and not 'latest', and command is defined
.check_vars:
	@if [[ -z "${IMAGE_VERSION}" || "${IMAGE_VERSION}" == "latest" ]] ; then \
		echo -e "${RED}IMAGE_VERSION should be set to an unique value. i.e. ci-695 for Azure CI images. Current value='${IMAGE_VERSION}'${NOCOLOR}" ; \
		false; \
	fi

# Run test in Kubernetes. Image is formed as current-testenv-image:$IMAGE_VERSION
# IMAGE_VERSION needs to be defined outside, and correspond to existing (in REGISTRY) image
# For example, to run image generated on ci-695, use 'make test-withk8s IMAGE_VERSION=ci-695
K8S_RUNTEST := $(TOP)/cloud/k8s/tests/k8s-run-tests.sh
PROCESSED_COMPONENT_NAME := $(shell echo ${COMPONENT} | tr . -)
PROCESSED_IMAGE_VERION := $(shell echo $(IMAGE_VERSION) | tr '[A-Z]' '[a-z]')
TEST_K8S_IMAGE := $(REGISTRY)/test-$(COMPONENT)-$(DTYPE):$(IMAGE_VERSION)
TEST_K8S_NAME := test-${PROCESSED_COMPONENT_NAME}-$(DTYPE)-${PROCESSED_IMAGE_VERION}
ifneq (${K8S_TEST_ERR_NO_CLEANUP},)
	TEST_K8S_OPT := "err_no_cleanup"
else
	TEST_K8S_OPT := "default"
endif
test-withk8s: .check_vars ## run tests on a k8s cluster
	${K8S_RUNTEST} ${TEST_K8S_OPT} "${TEST_K8S_IMAGE}" "${TEST_K8S_NAME}" "${CONTAINER_TEST_CMD}"

test-all-withk8s: .check_vars
	${K8S_RUNTEST} ${TEST_K8S_OPT} "${TEST_K8S_IMAGE}" "${TEST_K8S_NAME}" "${CONTAINER_TEST_ALL_CMD}"

# Manual version... helpful when debugging failed CI runs by starting new pod from CI testenv-image
# Adds 'user-' prefix to names and puts container to sleep so we can exec into it
test-withk8s-manual: .check_vars ## same as test-withk8s, but allow for manual inspection afterwards
	${K8S_RUNTEST} "manual" "${TEST_K8S_IMAGE}" "${USER}-${TEST_K8S_NAME}" "${CONTAINER_TEST_CMD}"

test-all-withk8s-manual: .check_vars ## same as test-withk8s, but allow for manual inspection afterwards
	${K8S_RUNTEST} "manual" "${TEST_K8S_IMAGE}" "${USER}-${TEST_K8S_NAME}" "${CONTAINER_TEST_ALL_CMD}"


ifeq (${NO_RUNENV}, false)
K8S_RUN_VALIDATION := $(TOP)/cloud/k8s/tests/k8s-run-validation.sh
VALIDATION_K8S_IMAGE := $(RUNENV_DEMO_IMG_REG):$(IMAGE_VERSION)
VALIDATION_K8S_NAME := validation-${PROCESSED_COMPONENT_NAME}-${PROCESSED_IMAGE_VERION}

# We use the demo-runenv image for validation. Runenv only contain the bare
# minimum and no other application, so we use the demo-runenv image which
# contains all the applications.
validate-runenv-withk8s: .check_vars
	${K8S_RUN_VALIDATION} "${VALIDATION_K8S_IMAGE}" "${VALIDATION_K8S_NAME}"

endif # ifeq (${NO_RUNENV}, false)

${BLDDIR}:
	mkdir -p ${BLDDIR}

# install stuff for fedora per buildenv-image info. Assume buildenv-image either built or pulled
buildenv-local-fedora: ${KM_OPT_RT} .buildenv-local-dnf .buildenv-local-lib ## make local build environment for KM
	@if ! docker image inspect ${BUILDENV_IMG} > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG} is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi
	sudo dnf install -y `docker history --format "{{ .CreatedBy }}" --no-trunc ${BUILDENV_IMG} | sed -rn '/dnf install/s/.*dnf install -y([^&]*)(.*)/\1/p'`

# Get a list of DNF packages from buildenv-image and install it on the host
.buildenv-local-dnf: .buildenv-local-check-image
	sudo dnf install -y `docker history --format "{{ .CreatedBy }}" --no-trunc ${BUILDENV_IMG}:$(BUILDENV_IMAGE_VERSION) | sed -rn '/dnf install/s/.*dnf install -y([^&]*)(.*)/\1/p'`

# Fetches alpine libs, and preps writeable 'runtime' dir.
# It'd a prerequisite for all further builds and needs to be called right after building
# or pull the buildenv-image. Call it via 'make buildenv-local-fedora' or 'make .buildenv-local-lib'
# so that libs are on the host and can be copied to runenv-image and testenv-image
.buildenv-local-lib: ${KM_OPT_RT} ${KM_OPT_BIN} .buildenv-local-check-image
	docker create --name tmp_env $(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION)
	sudo docker cp tmp_env:/opt/kontain /opt
	docker rm tmp_env

.buildenv-local-check-image:
	@if ! docker image inspect ${BUILDENV_IMG}:$(BUILDENV_IMAGE_VERSION) > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG}:$(BUILDENV_IMAGE_VERSION) is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi

.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'
