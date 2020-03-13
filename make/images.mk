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
RUNENV_DEMO_IMG := ${RUNENV_IMG}-demo

# image names with proper registry
TEST_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(TEST_IMG))
BUILDENV_IMG_REG := $(subst kontain/,$(REGISTRY)/,$(BUILDENV_IMG))
# runenv images are pushed to dockerhub public registry.
RUNENV_REG := docker.io/kontainapp
RUNENV_IMG_REG := $(subst kontain/,$(RUNENV_REG)/,$(RUNENV_IMG))
RUNENV_DEMO_IMG_REG := $(subst kontain/,$(RUNENV_REG)/,$(RUNENV_DEMO_IMG))

TEST_DOCKERFILE ?= test-${DTYPE}.dockerfile
BUILDENV_DOCKERFILE ?= buildenv-${DTYPE}.dockerfile
RUNENV_DOCKERFILE ?= runenv.dockerfile
RUNENV_DEMO_DOCKERFILE ?= runenv-demo.dockerfile

# Path 'docker build' uses for build, test and run environments
BUILDENV_PATH ?= .
TESTENV_PATH ?= .
RUNENV_PATH ?= ${BLDDIR}
RUNENV_DEMO_PATH ?= .

TESTENV_EXTRA_FILES = ${KM_BIN} ${KM_LDSO}
testenv-image: ## build test image with test tools and code
	@# Copy KM there. TODO - remove when we support pre-installed KM
	$(foreach f,${TESTENV_EXTRA_FILES}, cp $f ${TESTENV_PATH};)
	${DOCKER_BUILD} --no-cache \
			--build-arg branch=${SRC_SHA} \
			--build-arg=BUILDENV_IMAGE_VERSION=${BUILDENV_IMAGE_VERSION} \
			-t ${TEST_IMG}:${IMAGE_VERSION} \
			${TESTENV_PATH} -f ${TEST_DOCKERFILE}
	$(foreach f,${TESTENV_EXTRA_FILES},rm ${TESTENV_PATH}/$(notdir $f);)

buildenv-image: ## make build image based on ${DTYPE}
	${DOCKER_BUILD} -t ${BUILDENV_IMG}:${BUILDENV_IMAGE_VERSION} ${BUILDENV_PATH} -f ${BUILDENV_DOCKERFILE}

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

# Helper when we need to make sure IMAGE_VERSION is defined and not 'latest', and command is defined
.check_vars:
	@if [[ -z "${IMAGE_VERSION}" || "${IMAGE_VERSION}" == latest ]] ; then \
		echo -e "${RED}IMAGE_VERSION should be set to something existing in Registry, e.g. ci-695. Current value='${IMAGE_VERSION}'${NOCOLOR}" ; \
		false; \
	fi
	@if [[ -z "${CONTAINER_TEST_CMD}" ]] ; then \
		echo -e "${RED}CONTAINER_TEST_CMD needs to be defined to use this target${NOCOLOR}"; \
		false; \
	fi

# We generate test pod specs from this template
TEST_POD_TEMPLATE := $(TOP)/cloud/k8s/test-pod-template.yaml

# CONTAINER_TEST_CMD could be overriden, let's keep the original one so we can use it for help messages
CONTAINER_TEST_CMD_HELP := $(CONTAINER_TEST_CMD)

# Pod spec needs a json list as a container's command, so generate it first from a "command params" string
__CONTAINER_TEST_CMD = $(wordlist 2,100,$(foreach item,$(CONTAINER_TEST_CMD), , \"$(item)\"))

# Run full suite of tests regarless of being asked to run full or abbreviated set
test-withdocker: ## Run tests in local Docker. IMAGE_VERSION (i.e. tag) needs to be passed in
	${DOCKER_RUN_TEST} ${TEST_IMG}:${IMAGE_VERSION} ${CONTAINER_TEST_CMD}

# define commands to preprocess kubernetes pod template and pass to 'kubectl apply'.
export define preprocess_and_apply
m4 -D NAME="$(USER_NAME)test-$(COMPONENT)-$(DTYPE)-$(shell echo $(IMAGE_VERSION) | tr [A-Z] [a-z])" \
	-D IMAGE="$(REGISTRY)/test-$(COMPONENT)-$(DTYPE):$(IMAGE_VERSION)" \
	-D COMMAND="$(__CONTAINER_TEST_CMD)" $(TEST_POD_TEMPLATE) | kubectl apply -f - -o jsonpath='{.metadata.name}'
endef

# Run test in Kubernetes. Image is formed as current-testenv-image:$IMAGE_VERSION
# IMAGE_VERSION needs to be defined outside, and correspond to existing (in REGISTRY) image
# For example, to run image generated on ci-695, use 'make test-withk8s IMAGE_VERSION=ci-695
test-withk8s :  .check_vars ## Run tests in Kubernetes. IMAGE_VERSION need to be passed
	@echo '$(preprocess_and_apply)'
	@name=$$($(preprocess_and_apply)) ;\
		echo -e "Run bash in your pod '$$name' using '${GREEN}kubectl exec $$name -it -- bash${NOCOLOR}'" ; \
		echo -e "Run tests inside your pod using '${GREEN}${CONTAINER_TEST_CMD_HELP}${NOCOLOR}'" ; \
		echo -e "When you are done, do not forget to '${GREEN}kubectl delete pod $$name${NOCOLOR}'"

# Manual version... helpful when debugging failed CI runs by starting new pod from CI testenv-image
# Adds 'user-' prefix to names and puts container to sleep so we can exec into it
test-withk8s-manual : .test-withk8s-manual ## create pod with existing testenv image for manual debug. e.g. 'make test-withk8s-manual IMAGE_VERSION=ci-695'
ifneq ($(findstring test-withk8s-manual,${MAKECMDGOALS}),)
USER_NAME := $(shell id -un)-
POD_TTL_SEC := 3600
override CONTAINER_TEST_CMD := sleep ${POD_TTL_SEC}
.test-withk8s-manual : test-withk8s ## create pod with existing testenv image for manual debug. e.g. 'make test-withk8s-manual IMAGE_VERSION=ci-695'
endif

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
	@-docker rmi -f ${RUNENV_IMG}:latest 2>/dev/null
ifdef runenv_prep
	@echo -e "Executing prep steps"
	eval $(runenv_prep)
endif
	${DOCKER_BUILD} -t ${RUNENV_IMG}:${IMAGE_VERSION} -f ${RUNENV_DOCKERFILE} ${RUNENV_PATH}
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls ${RUNENV_IMG} --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"

ifneq (${RUNENV_VALIDATE_SCRIPT},)
SCRIPT_MOUNT = -v ${RUNENV_VALIDATE_SCRIPT}:/scripts/$(notdir ${RUNENV_VALIDATE_SCRIPT}):z
VALIDATE_CMD = /scripts/$(notdir ${RUNENV_VALIDATE_SCRIPT})
else
VALIDATE_CMD = ${RUNENV_VALIDATE_CMD}
endif
# We use km from ${KM_BIN} here from the build tree instead of what's on the host under ${KM_OPT_BIN}.
validate-runenv-image: ## Validate runtime image
	${DOCKER_RUN_TEST} \
		-v ${KM_BIN}:${KM_OPT_BIN}:z \
		${SCRIPT_MOUNT} \
		${RUNENV_IMG}:${IMAGE_VERSION} \
		${VALIDATE_CMD}

push-runenv-image:  ## pushes image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(RUNENV_IMG):$(IMAGE_VERSION) TO=$(RUNENV_IMG_REG):$(IMAGE_VERSION)

pull-runenv-image: ## pulls test image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(RUNENV_IMG_REG):$(IMAGE_VERSION) TO=$(RUNENV_IMG):$(IMAGE_VERSION)

distro: runenv-image ## an alias for runenv-image
publish: push-runenv-image

runenv-demo-image:
ifeq ($(shell test -e ${RUNENV_DEMO_DOCKERFILE} && echo -n yes),yes)
	@-docker rmi -f ${RUNENV_DEMO_IMG}:${IMAGE_VERSION} 2>/dev/null
	${DOCKER_BUILD} -t ${RUNENV_DEMO_IMG}:${IMAGE_VERSION} -f ${RUNENV_DEMO_DOCKERFILE} ${RUNENV_DEMO_PATH}
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls ${RUNENV_DEMO_IMG} --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"
else
	@echo -e "No demo dockerfile ${RUNENV_DEMO_DOCKERFILE} define. Skipping..."
endif

push-runenv-demo-image:
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(RUNENV_DEMO_IMG):$(IMAGE_VERSION) TO=$(RUNENV_DEMO_IMG_REG):$(IMAGE_VERSION)

endif # ifeq (${NO_RUNENV},)

${BLDDIR}:
	mkdir -p ${BLDDIR}

.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'
