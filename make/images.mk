#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# Support for building docker images.
#
# Builds 3 type of images
#  - buildenv-image (environment for build)
#  - testenv-image (all artifacts for running build suites, including KM and payloads)
#  - runenv-image (minimal image for running KM+payload)
#  - demo-runenv-image (image to test the runenv-image. Contains test scrpis, might contain an app, hence the `demo-`
#
# The first 2 are explained in docs/build.md and docs/image-targets.md
#
# The runenv-image is a bare bones image for specific payload.
# 		Can be built with 'make runenv-image'. The following info needs to be defined in Makefile for it to work
#		- BUILDENV_PATH is the location for Docker to use. Default is build/payloads/component
#		- runenv_prep function is (optional) code to copy stuff to the BUILDENV_PATH, or modify it
#				before running Docker. E.g. if BUILDENV_PATH=. , the runenv_prpe is likely not needed
#		- COMPONENT, PAYLOAD_NAME and PAYLOAD_KM also need to be defined. See payloads/node
#		for examples
#     Same target builds demo-runenv image to validate
#

ifeq ($(COMPONENT),)
$(error "COMPONENT is undefined - please add COMPONENT=component_name in your Makefile")
endif

include ${TOP}/make/locations.mk

# Image names and location for image builds
TEST_IMG := kontainapp/test-${COMPONENT}-${DTYPE}
TEST_IMG_TAGGED := ${TEST_IMG}:${IMAGE_VERSION}

COVERAGE_TEST_IMG := kontainapp/coverage-test-${COMPONENT}-${DTYPE}
COVERAGE_TEST_IMG_TAGGED := ${COVERAGE_TEST_IMG}:${IMAGE_VERSION}

VALGRIND_TEST_IMG := kontainapp/valgrind-test-${COMPONENT}-${DTYPE}
VALGRIND_TEST_IMG_TAGGED := ${VALGRIND_TEST_IMG}:${IMAGE_VERSION}

BUILDENV_IMG := kontainapp/buildenv-${COMPONENT}-${DTYPE}
BUILDENV_IMG_TAGGED := ${BUILDENV_IMG}:${BUILDENV_IMAGE_VERSION}

# runenv does not include anything linix distro specific, so it does not have 'DTYPE'
RUNENV_IMG := kontainapp/runenv-${COMPONENT}
RUNENV_IMG_TAGGED := ${RUNENV_IMG}:${IMAGE_VERSION}

# runenv demo image produce a demo based on the runenv image
RUNENV_DEMO_IMG := kontainapp/demo-runenv-${COMPONENT}
RUNENV_DEMO_IMG_TAGGED := ${RUNENV_DEMO_IMG}:${IMAGE_VERSION}

# image names with proper registry
TEST_IMG_REG := $(subst kontainapp/,$(REGISTRY)/,$(TEST_IMG))
BUILDENV_IMG_REG := $(subst kontainapp/,$(REGISTRY)/,$(BUILDENV_IMG))

COVERAGE_TEST_IMG_REG := $(subst kontainapp/,$(REGISTRY)/,$(COVERAGE_TEST_IMG))
VALGRIND_TEST_IMG_REG := $(subst kontainapp/,$(REGISTRY)/,$(VALGRIND_TEST_IMG))

RUNENV_IMG_REG := $(subst kontainapp/,$(REGISTRY)/,$(RUNENV_IMG))
RUNENV_DEMO_IMG_REG := $(subst kontainapp/,$(REGISTRY)/,$(RUNENV_DEMO_IMG))

TEST_DOCKERFILE ?= test-${DTYPE}.dockerfile
BUILDENV_DOCKERFILE ?= buildenv-${DTYPE}.dockerfile
RUNENV_DOCKERFILE ?= runenv.dockerfile
DEMO_RUNENV_DOCKERFILE ?= demo-runenv.dockerfile

# Path 'docker build' uses for build, test and run environments
BUILDENV_PATH ?= .
TESTENV_PATH ?= .
RUNENV_PATH ?= ${BLDDIR}
RUNENV_DEMO_PATH ?= .


define testenv_prep =
	$(call testenv_preprocess)
	tar -czf ${TESTENV_PATH}/extras.tar.gz \
		-C ${TOP} \
			build/opt/kontain/runtime/libc.so \
			build/opt/kontain/runtime/ld-linux-x86-64.so.2 \
			build/opt/kontain/alpine-lib/usr/lib/libstdc++.so.6 \
			build/opt/kontain/alpine-lib/usr/lib/libstdc++.so.6.0.28 \
			build/opt/kontain/alpine-lib/usr/lib/libgcc_s.so.1 \
			build/opt/kontain/lib/libmimalloc.so \
			build/opt/kontain/lib/libmimalloc.so.1.7 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.6 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.7 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.7.1.0 \
			build/opt/kontain/alpine-lib/usr/lib/libgcc_s.so \
			build/opt/kontain/runtime/libpthread.so \
			build/opt/kontain/bin/km \
			build/opt/kontain/bin/km_cli \
			build/opt/kontain/bin/km_guest_asmcode.o \
			km \
			include \

	$(if ${TESTENV_EXTRA_FILES},cp -r --preserve=links ${TESTENV_EXTRA_FILES} ${TESTENV_PATH})
endef

define coverage_testenv_prep =
	$(call testenv_preprocess)
	tar -czf ${TESTENV_PATH}/extras.tar.gz \
		--transform='s/coverage\/bin/bin/g' \
		-C ${TOP} \
			build/opt/kontain/runtime/libc.so \
			build/opt/kontain/runtime/ld-linux-x86-64.so.2 \
			build/opt/kontain/alpine-lib/usr/lib/libstdc++.so.6 \
			build/opt/kontain/alpine-lib/usr/lib/libstdc++.so.6.0.28 \
			build/opt/kontain/alpine-lib/usr/lib/libgcc_s.so.1 \
			build/opt/kontain/lib/libmimalloc.so \
			build/opt/kontain/lib/libmimalloc.so.1.7 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.6 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.7 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.7.1.0 \
			build/opt/kontain/alpine-lib/usr/lib/libgcc_s.so \
			build/opt/kontain/runtime/libpthread.so \
			build/opt/kontain/coverage/bin/km \
			build/opt/kontain/coverage/bin/km_cli \
			build/opt/kontain/coverage/bin/km_guest_asmcode.o \
			build/km/coverage \
			km \
			include \

	$(if ${TESTENV_EXTRA_FILES},cp -r --preserve=links ${TESTENV_EXTRA_FILES} ${TESTENV_PATH})
endef

define valgrind_testenv_prep =
	$(call testenv_preprocess)
	tar -czf ${TESTENV_PATH}/extras.tar.gz \
		--transform='s/valgrind\/bin/bin/g' \
		-C ${TOP} \
			build/opt/kontain/runtime/libc.so \
			build/opt/kontain/runtime/ld-linux-x86-64.so.2 \
			build/opt/kontain/alpine-lib/usr/lib/libstdc++.so.6 \
			build/opt/kontain/alpine-lib/usr/lib/libstdc++.so.6.0.28 \
			build/opt/kontain/alpine-lib/usr/lib/libgcc_s.so.1 \
			build/opt/kontain/lib/libmimalloc.so \
			build/opt/kontain/lib/libmimalloc.so.1.7 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.6 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.7 \
			build/opt/kontain/alpine-lib/usr/lib/libffi.so.7.1.0 \
			build/opt/kontain/alpine-lib/usr/lib/libgcc_s.so \
			build/opt/kontain/runtime/libpthread.so \
			build/opt/kontain/valgrind/bin/km \
			build/opt/kontain/valgrind/bin/km_cli \
			build/opt/kontain/valgrind/bin/km_guest_asmcode.o \
			build/km/valgrind \
			km \
			include \

	$(if ${TESTENV_EXTRA_FILES},cp -r --preserve=links ${TESTENV_EXTRA_FILES} ${TESTENV_PATH})
endef

testenv_cleanup = rm ${TESTENV_PATH}/extras.tar.gz
testenv_cleanup_extras=$(if ${TESTENV_EXTRA_FILES}, @for f in ${TESTENV_EXTRA_FILES}; do f=$$(basename $${f}); echo "rm -rf ${TESTENV_PATH}/$${f}"; rm -rf ${TESTENV_PATH}/$${f}; done )

## build test image with test tools and code
testenv-image :
	$(call clean_container_image,${TEST_IMG_TAGGED})
	$(call testenv_prep)
	${DOCKER_BUILD} --no-cache \
			--build-arg=branch=${SRC_SHA} \
			--build-arg=BUILDENV_IMAGE_VERSION=${BUILDENV_IMAGE_VERSION} \
			--build-arg=IMAGE_VERSION=${IMAGE_VERSION} \
			--build-arg=MODE=${BUILD} \
			${TESTENV_IMAGE_EXTRA_ARGS} \
			-t ${TEST_IMG_TAGGED} \
			-f ${TEST_DOCKERFILE} \
			${TESTENV_PATH}
	$(call testenv_cleanup)
	$(call testenv_cleanup_extras)

coverage-testenv-image:
	$(call clean_container_image,${COVERAGE_TEST_IMG_TAGGED})
	$(call coverage_testenv_prep)
	${DOCKER_BUILD} --no-cache \
			--build-arg=branch=${SRC_SHA} \
			--build-arg=BUILDENV_IMAGE_VERSION=${BUILDENV_IMAGE_VERSION} \
			--build-arg=IMAGE_VERSION=${IMAGE_VERSION} \
			--build-arg=MODE=${BUILD} \
			${TESTENV_IMAGE_EXTRA_ARGS} \
			-t ${COVERAGE_TEST_IMG_TAGGED} \
			-f ${TEST_DOCKERFILE} \
			${TESTENV_PATH}
	$(call testenv_cleanup)
	$(call testenv_cleanup_extras)

valgrind-testenv-image:
	$(call clean_container_image,${VALGRIND_TEST_IMG_TAGGED})
	$(call valgrind_testenv_prep)
	${DOCKER_BUILD} --no-cache \
			--build-arg=branch=${SRC_SHA} \
			--build-arg=BUILDENV_IMAGE_VERSION=${BUILDENV_IMAGE_VERSION} \
			--build-arg=IMAGE_VERSION=${IMAGE_VERSION} \
			--build-arg=MODE=${BUILD} \
			${TESTENV_IMAGE_EXTRA_ARGS} \
			-t ${VALGRIND_TEST_IMG_TAGGED} \
			-f ${TEST_DOCKERFILE} \
			${TESTENV_PATH}
	$(call testenv_cleanup)
	$(call testenv_cleanup_extras)

buildenv-image: ${BLDDIR} ## make build image based on ${DTYPE}
	${DOCKER_BUILD} --no-cache -t ${BUILDENV_IMG_TAGGED} \
		--build-arg=BUILDENV_IMAGE_VERSION=${BUILDENV_IMAGE_VERSION} \
		${BUILDENV_IMAGE_EXTRA_ARGS} \
		${BUILDENV_PATH} -f ${BUILDENV_DOCKERFILE}

push-testenv-image: testenv-image ## pushes image. Blocks ':latest' - we dont want to step on each other
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_image_version .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(TEST_IMG):$(IMAGE_VERSION) TO=$(TEST_IMG_REG):$(IMAGE_VERSION)

push-coverage-testenv-image: coverage-testenv-image ## pushes image. Blocks ':latest' - we dont want to step on each other
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_image_version .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(COVERAGE_TEST_IMG):$(IMAGE_VERSION) TO=$(COVERAGE_TEST_IMG_REG):$(IMAGE_VERSION)

push-valgrind-testenv-image: valgrind-testenv-image ## pushes image. Blocks ':latest' - we dont want to step on each other
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_image_version .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(VALGRIND_TEST_IMG):$(IMAGE_VERSION) TO=$(VALGRIND_TEST_IMG_REG):$(IMAGE_VERSION)

pull-testenv-image: ## pulls test image. Mainly need for CI
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_image_version .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(TEST_IMG_REG):$(IMAGE_VERSION) TO=$(TEST_IMG):$(IMAGE_VERSION)

pull-coverage-testenv-image: ## pulls test image. Mainly need for CI
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_image_version .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(COVERAGE_TEST_IMG_REG):$(IMAGE_VERSION) TO=$(COVERAGE_TEST_IMG):$(IMAGE_VERSION)

pull-valgrind-testenv-image: ## pulls test image. Mainly need for CI
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .check_image_version .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(VALGRIND_TEST_IMG_REG):$(IMAGE_VERSION) TO=$(VALGRIND_TEST_IMG):$(IMAGE_VERSION)

# a few of Helpers for push/pull image and re-tag
.push-image:
	@if [ -z "$(FROM)" -o -z "$(TO)" ] ; then echo Missing params FROM or TO, exiting; false; fi
	docker tag $(FROM) $(TO)
	docker push $(TO)

.pull-image:
	@echo -e "$(CYAN)Do not forget to login to azure using 'make -C GIT_TOP/cloud/azure login'$(NOCOLOR)"
	@if [ -z "$(FROM)" -o -z "$(TO)" ] ; then echo Missing params FROM or TO, exiting; false; fi
	docker pull --quiet $(FROM)
	docker tag $(FROM) $(TO)
	@echo -e "Pulled image ${GREEN}${TO}${NOCOLOR}"

# Build env image push. For this target to work, set FORCE_BUILDENV_PUSH to 'force'. Also set IMAGE_VERSION
# to the version you want to push. BE CAREFUL - it pushes to shared image !!!
push-buildenv-image: ## Pushes to buildnev image. PROTECTED TARGET
	@if [[ "${BUILDENV_IMAGE_VERSION}" == "latest" && "$(FORCE_BUILDENV_PUSH)" != "force" ]] ; then \
		echo -e "$(RED)Unforced push of 'latest' buildenv images is not supported without setting FORCE_BUILDENV_PUSH to force$(NOCOLOR)" && false; fi
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		FROM=$(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION) TO=$(BUILDENV_IMG_REG):$(BUILDENV_IMAGE_VERSION)
	@echo -e "$(YELLOW)Reminder: you may also need to update AWS AMI. Check on slack #engineering for steps$(NOCOLOR)"

pull-buildenv-image: ## Pulls the buildenv image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		FROM=$(BUILDENV_IMG_REG):$(BUILDENV_IMAGE_VERSION) TO=$(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION)

NO_RUNENV ?= false
ifeq (${NO_RUNENV}, false)

runenv-image demo-runenv-image: ${RUNENV_PATH} ${KM_BIN} ${RUNENV_VALIDATE_DEPENDENCIES} ## Build minimal runtime image
	@$(TOP)/make/check-docker.sh
	$(call clean_container_image,${RUNENV_IMG_TAGGED})
ifdef runenv_prep
	@echo -e "Executing prep steps"
	eval $(runenv_prep)
endif
	${DOCKER_BUILD} \
		${RUNENV_IMAGE_EXTRA_ARGS} \
		--build-arg RUNENV_IMAGE_VERSION=${IMAGE_VERSION} \
		-t ${RUNENV_IMG_TAGGED} \
		-f ${RUNENV_DOCKERFILE} \
		${RUNENV_PATH}
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls ${RUNENV_IMG} --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"
ifeq ($(shell test -e ${DEMO_RUNENV_DOCKERFILE} && echo -n yes),yes)
	$(call clean_container_image,${RUNENV_DEMO_IMG_TAGGED})
	${DOCKER_BUILD} \
		-t ${RUNENV_DEMO_IMG_TAGGED} \
		--build-arg RUNENV_IMAGE_VERSION=${IMAGE_VERSION} \
		-f ${DEMO_RUNENV_DOCKERFILE} \
		${RUNENV_DEMO_PATH}
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls ${RUNENV_DEMO_IMG} --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"
else
	@echo -e "No demo dockerfile ${DEMO_RUNENV_DOCKERFILE} define. Skipping..."
endif

# array syntax to preserve space in the args`
RUNENV_VALIDATE_CMD ?= ("Place" "ValidateCommandBashArray" "Here")
RUNENV_VALIDATE_EXPECTED ?= Hello

# We use km from ${KM_BIN} here from the build tree instead of what's on the host under ${KM_OPT_BIN}.
# We remove the container image from the podman local repository to force podman to get it from the
# docker local repository since that is where the payload images are stored when built.
# There does not appear to a way to stop podman from complaining if the image being deleted does not exist.
validate-runenv-image: ## Validate runtime image
	tmp_bash_array=${RUNENV_VALIDATE_CMD} && \
	${DOCKER_RUN} ${DOCKER_INTERACTIVE} --init ${DOCKER_KRUN_RUNTIME} \
	${RUNENV_DEMO_IMG_TAGGED} \
	"$${tmp_bash_array[@]}" 2>&1 | grep "${RUNENV_VALIDATE_EXPECTED}"
	@-podman image rm ${RUNENV_DEMO_IMG_TAGGED} >/dev/null 2>&1
	tmp_bash_array=${RUNENV_VALIDATE_CMD} && \
	${PODMAN_RUN_TEST} ${PODMAN_KRUN_RUNTIME} \
	docker-daemon:${RUNENV_DEMO_IMG_TAGGED} \
	"$${tmp_bash_array[@]}" 2>&1 | grep "${RUNENV_VALIDATE_EXPECTED}"

push-runenv-image: runenv-image ## pushes image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(RUNENV_IMG):$(IMAGE_VERSION) TO=$(RUNENV_IMG_REG):$(IMAGE_VERSION)

pull-runenv-image: ## pulls test image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(RUNENV_IMG_REG):$(IMAGE_VERSION) TO=$(RUNENV_IMG_TAGGED)

distro: runenv-image ## an alias for runenv-image
publish: push-runenv-image ## an alias for push-runenv-image

push-demo-runenv-image: demo-runenv-image
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=${RUNENV_DEMO_IMG_TAGGED} TO=$(RUNENV_DEMO_IMG_REG):$(IMAGE_VERSION)

pull-demo-runenv-image: ## pulls test image.
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .pull-image \
		IMAGE_VERSION="$(IMAGE_VERSION)" \
		FROM=$(RUNENV_DEMO_IMG_REG):$(IMAGE_VERSION) TO=$(RUNENV_DEMO_IMG_TAGGED)

endif # ifeq (${NO_RUNENV},)

# Default to something that gurantee to fail.
CONTAINER_TEST_CMD ?= \
	$(shell echo -e "${RED}CONTAINER_TEST_CMD needs to be defined to use this target${NOCOLOR}"); \
	false;

CONTAINER_TEST_ALL_CMD ?= ${CONTAINER_TEST_CMD}

DOCKER_COVERAGE_SCRIPT := ${DOCKER_KM_TOP}/tests/scripts/coverage/coverage.sh
DOCKER_UPLOAD_COVERAGE_SCRIPT := ${DOCKER_KM_TOP}/tests/scripts/coverage/upload-coverage.sh

test-withdocker: ## Run tests in local Docker. IMAGE_VERSION (i.e. tag) needs to be passed in
	${DOCKER_RUN_TEST} ${TEST_IMG_TAGGED} ${CONTAINER_TEST_CMD}

test-all-withdocker: ## a special helper to run more node.km tests.
	${DOCKER_RUN_TEST} ${TEST_IMG_TAGGED} ${CONTAINER_TEST_ALL_CMD}

test-valgrind-withdocker:
	${DOCKER_RUN_TEST} ${VALGRIND_TEST_IMG_TAGGED} ${CONTAINER_VARGRIND_TEST_CMD}

test-coverage-withdocker: ## Run tests in local Docker. IMAGE_VERSION (i.e. tag) needs to be passed in
	mkdir -p ${COVERAGE_KM_BLDDIR}
	chmod o+w ${COVERAGE_KM_BLDDIR}
	${DOCKER_RUN_TEST} --name covcontainer_${IMAGE_VERSION} -v ${COVERAGE_KM_BLDDIR}:/tmp ${COVERAGE_TEST_IMG_TAGGED} \
		/bin/bash -c "if [ -f /tmp/km_main.gcda ];then cp /tmp/*.gcda  ${DOCKER_COVERAGE_KM_BLDDIR}; fi\
			 	&& ${CONTAINER_TEST_CMD} \
				&& cp ${DOCKER_COVERAGE_KM_BLDDIR}/*.gcda /tmp"
	chmod o-w ${COVERAGE_KM_BLDDIR}

upload-coverage-withdocker: .check_image_version
	mkdir -p ${COVERAGE_KM_BLDDIR}
	${DOCKER_RUN} --name covcontainer_${IMAGE_VERSION} -v ${COVERAGE_KM_BLDDIR}:/tmp ${COVERAGE_TEST_IMG_TAGGED} \
		/bin/bash -c "if [ -f /tmp/km_main.gcda ];then cp /tmp/*.gcda  ${DOCKER_COVERAGE_KM_BLDDIR}; fi\
			&& ls -l ${DOCKER_COVERAGE_KM_BLDDIR} \
			&& ${DOCKER_COVERAGE_SCRIPT} ${DOCKER_KM_TOP}/km ${DOCKER_COVERAGE_KM_BLDDIR} ${DOCKER_COVERAGE_KM_BLDDIR} \
			&&${DOCKER_UPLOAD_COVERAGE_SCRIPT} ${DOCKER_COVERAGE_KM_BLDDIR} ${IMAGE_VERSION} ${GITHUB_TOKEN}"


# === BUILDENV LOCAL

${BLDDIR}:
	mkdir -p ${BLDDIR}

# install stuff for fedora per buildenv-image info. Assume buildenv-image either built or pulled
buildenv-local-fedora: .buildenv-local-dnf .buildenv-local-lib ## make local build environment for KM

# Get a list of DNF packages from buildenv-image and install it on the host
.buildenv-local-dnf: .buildenv-local-check-image
	sudo dnf install -y `docker history --format "{{ .CreatedBy }}" --no-trunc ${BUILDENV_IMG_TAGGED} | sed -rn '/dnf install/s/.*dnf install -y([^&]*)(.*)/\1/p'` \
	                 |& grep -v 'is already installed'

# Fetches alpine libs, and preps writeable 'runtime' dir.
# It'd a prerequisite for all further builds and needs to be called right after building
# or pull the buildenv-image. Call it via 'make buildenv-local-fedora' or 'make .buildenv-local-lib'
# so that libs are on the host and can be copied to runenv-image and testenv-image
.buildenv-local-lib: .buildenv-local-check-image | ${KM_OPT_RT} ${KM_OPT_BIN} ${KM_OPT_COVERAGE_BIN} ${KM_OPT_VALGRIND_BIN} ${KM_OPT_INC} ${KM_OPT_LIB}
	docker create --name tmp_env ${BUILDENV_IMG_TAGGED}
	sudo docker cp tmp_env:/opt/kontain/. ${KM_OPT}
	sudo chown --recursive ${CURRENT_UID}:${CURRENT_GID} ${BLDTOP}
	docker rm tmp_env

.buildenv-local-check-image:
	@if ! docker image inspect ${BUILDENV_IMG_TAGGED} > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG_TAGGED} is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi


DOCKER_REG ?= docker.io
RELEASE_REG = ${DOCKER_REG}/kontainapp
publish-runenv-image:
	for tag in ${RUNENV_TAG} ; do \
		$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
			IMAGE_VERSION="$(IMAGE_VERSION)"\
			FROM=$(RUNENV_IMG_TAGGED) TO=$(RELEASE_REG)/runenv-$$tag:${SRC_SHA}; \
	done
	for tag in ${RUNENV_TAG} ; do \
		$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
			IMAGE_VERSION="$(IMAGE_VERSION)"\
			FROM=$(RUNENV_IMG_TAGGED) TO=$(RELEASE_REG)/runenv-$$tag:latest; \
	done
ifdef RELEASE_TAG
	for tag in ${RUNENV_TAG} ; do \
		$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
			IMAGE_VERSION="$(IMAGE_VERSION)"\
			FROM=$(RUNENV_IMG_TAGGED) TO=$(RELEASE_REG)/runenv-$$tag:${RELEASE_TAG}; \
	done
endif

publish-buildenv-image:
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(BUILDENV_IMG_TAGGED) TO=$(DOCKER_REG)/$(BUILDENV_IMG_TAGGED); \
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		IMAGE_VERSION="$(IMAGE_VERSION)"\
		FROM=$(BUILDENV_IMG_TAGGED) TO=$(DOCKER_REG)/$(BUILDENV_IMG):${SRC_SHA}; \

.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'
