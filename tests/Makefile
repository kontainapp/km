#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

TOP := $(shell git rev-parse --show-toplevel)
CURRENT_DIR := ${TOP}/tests
COMPONENT := km

NO_RUNENV = true
# Build somewhere with very few files to avoid sending them to dockerd
BUILDENV_PATH = ${BLDDIR}

# Unfortunately a few vars have to be defined BEFORE this include, and much of 'coverage'
# targets should be AFTER this include, due to 'ifdef`
# in include file's ifdef() handled during file readby Make during file read.
# So this include is stuck here in the middle of the file by design.
include ${TOP}/make/images.mk

# We use coverage version of km to test. 
TESTENV_EXTRA_FILES := ${COVERAGE_KM_BIN} ${KM_LDSO}

# We need to put both KM_BIN and COVERAGE_KM_BIN into the testenv. Since km
# binaries have the same name, special care is required. As part of the copy,
# we rename the coverage km into km.coverage, so we won't have issue with
# copying both of them into the TESTENV_PATH. testenv.dockerfile will take care
# of copying the km into the right place.
override define testenv_prep =
	cp ${KM_BIN} ${TESTENV_PATH}
	cp ${KM_LDSO} ${TESTENV_PATH}
	cp ${COVERAGE_KM_BIN} ${TESTENV_PATH}/km.coverage
endef

override define testenv_cleanup =
	rm ./km
	rm ./libc.so
	rm km.coverage
endef

# this is how we run tests in containers:
CONTAINER_TEST_CMD :=  ${DOCKER_KM_TOP}/tests/run_bats_tests.sh --km=/opt/kontain/bin/km
CONTAINER_COVERAGE_TEST_CMD :=  ${DOCKER_KM_TOP}/tests/run_bats_tests.sh --km=/opt/kontain/bin/coverage/km

# Build from these sources. Note that test may be skipped using "skip" support in out bats files
SRC_C := $(wildcard *_test.c)
SRC_CPP := $(wildcard *_test.cpp)
HELPER_SRC := $(wildcard *_helper.c)

# Test driver and shared libs for KM dlopen test, plus extra stuff we need to buid .so from
SRC_SHR := dlopen_test.c dlopen_test_lib.c dlopen_test_lib2.c

DEBUG ?= -ggdb3
CFLAGS = -Wall -Werror $(COPTS) ${DEBUG} -fPIC -fno-stack-protector -pthread -I${TOP}/include -I${TOP}/km
CXXFLAGS=${CFLAGS}

LDLIBS  :=  -pthread -ldl -L. -lhelper
LDFLAGS := ${DEBUG}

DEPS = ${SRC_C:%.c=%.d} ${SRC_SHR:%.c=%.d} ${SRC_CPP:%.cpp=%.d}

SUFFIXES := .km .kmd .km.so .native.km .native.kmd
TESTS := ${SRC_C:%.c=%} ${SRC_CPP:%.cpp=%}
EXECS := ${TESTS} \
	$(foreach s,${SUFFIXES},$(addsuffix ${s},${TESTS}))

# misc. other .so files
SEXECS := ${SRC_SHR:%.c=%.so}

# Only tests matching this regexp will be run
MATCH ?= .*

all: ${EXECS} ${SEXECS} ## Build all test, for Linux and KM

${EXECS} ${SEXECS}: libhelper.a

libhelper.a: ${HELPER_SRC:%.c=%.o}
	rm -f $@; ar rv $@ $^

# ignore test failure when building coverage
ifneq ($(findstring coverage,${MAKECMDGOALS}),)
IGNORE_FAILURE := --ignore-failure
endif

test: all  ## Run all or some of tests. Use 'MATCH=<string>' to limit the set by name
	time ./run_bats_tests.sh --km=${KM_BIN} --pretty ${IGNORE_FAILURE} --match="${MATCH}"

test-all: test ## alias for test

KCC := kontain-gcc
KCXX := kontain-g++

# use g++ link libs when sources were C++
${SRC_CPP:%.cpp=%}: CC=${CXX}
${SRC_CPP:%.cpp=%.km}: KCC=${KCXX}
${SRC_CPP:%.cpp=%.kmd}: KCC=${KCXX}
${SRC_CPP:%.cpp=%.native.km}:KCC=${KCXX}
${SRC_CPP:%.cpp=%.native.kmd}: KCC=${KCXX}
${SRC_CPP:%.cpp=%.km.so}: KCC=${KCXX}

%.so: %.o
	${CC} -shared ${LDFLAGS} $< ${LDLIBS} -o $@

%.km: %.o
	${KCC} -static ${LDFLAGS} $< ${LDLIBS} -o $@

%.kmd: %.o
	${KCC} -dynamic ${LDFLAGS} $< ${LDLIBS} -o $@

%.native.km: %.o
	${KCC} -static -alpine ${LDFLAGS} $< ${LDLIBS} -o $@

%.native.kmd: %.o
	${KCC} -dynamic -alpine ${LDFLAGS} $< ${LDLIBS} -o $@

%.km.so: %.o
	${KCC} -shared ${LDFLAGS} $< -o $@ ${LDLIBS}

.PHONY: load_expected_size
load_expected_size: load_test.km # load_test.so
	@nm load_test.km | awk '/ _end/{ print "0x" $$1}'

%.d: %.c
	@# print the command we really need to run
	@echo $(CC) -MT $*.o -MT $@ -MM ${CFLAGS} $< -o $@
	@# run the command and adjust it's error messages so VS Code navigates properly
	@set -e; rm -f $@; \
	  $(CC) -MT $*.o -MT $@ -MM ${CFLAGS} $< -o $@ |& \
	  sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="
	@# make sure dependencies are rebuilt if needed.
	@# sed strips everything before colon, removes leading and trailing spaces, then splits words into individual lines
	@# and trailing ':'. See - http://scottmcpeak.com/autodepend/autodepend.html for explanation
	@sed -e 's/^.*: *//' -e 's/ *\\$$//'  -e 's/^ *//' -e 's/ \+/:\n/g'  -e 's/$$/:/' $*.d | cat $*.d - > $*.d.tmp
	@mv $*.d.tmp $*.d

%.d: %.cpp
	@# print the command we really need to run
	@echo $(CXX) -MT $*.o -MT $@ -MM ${CXXFLAGS} ${CPPFLAGS} $< -o $@
	@# run the command and adjust it's error messages so VS Code navigates properly
	@set -e; rm -f $@; \
	  $(CXX) -MT $*.o -MT $@ -MM ${CXXFLAGS} ${CPPFLAGS} $< -o $@ |& \
	  sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="
	@# make sure dependencies are rebuilt if needed.
	@# sed strips everything before colon, removes leading and trailing spaces, then splits words into individual lines
	@# and trailing ':'. See - http://scottmcpeak.com/autodepend/autodepend.html for explanation
	@sed -e 's/^.*: *//' -e 's/ *\\$$//'  -e 's/^ *//' -e 's/ \+/:\n/g'  -e 's/$$/:/' $*.d | cat $*.d - > $*.d.tmp
	@mv $*.d.tmp $*.d

clean:
	rm -f *.o *.d ${EXECS} ${SEXECS} kmcore libhelper.a

#
# do not generate .d file for some targets
#
$(shell [[ "${MAKECMDGOALS}" =~ ^${NO_DEPS_TARGETS}$$ || "${MAKEFLAGS}" =~ "n" ]] )
ifneq ($(.SHELLSTATUS),0)
-include ${DEPS}
endif

COVERAGE_SCRIPT := ${CURRENT_DIR}/scripts/coverage.sh
coverage: all ## Build with code coverage, then run tests and generate reports
	$(MAKE) BLDTYPE=$(COV_BLDTYPE) MAKEFLAGS="$(MAKEFLAGS)" .coverage

.coverage: test
	${COVERAGE_SCRIPT} ${TOP}/km ${KM_BLDDIR} ${KM_BLDDIR}

covclean: ## Clean up code coverage build artifacts
	$(MAKE) BLDTYPE=$(COV_BLDTYPE) MAKEFLAGS="$(MAKEFLAGS)" .cov_clean

.cov_clean:
	rm -rf ${BLDDIR}

# Since coverage is only relevent to km, put it here instead of image.mk
DOCKER_COVERAGE_SCRIPT := ${DOCKER_KM_TOP}/tests/scripts/coverage.sh
DOCKER_COVERAGE_CMD := ${DOCKER_COVERAGE_SCRIPT} ${DOCKER_KM_TOP}/km ${DOCKER_COVERAGE_KM_BLDDIR} ${DOCKER_COVERAGE_KM_BLDDIR}
DOCKER_COVERAGE_NAME := kontain-coverage-test
# Because coverage runs the test inside and generates .gcda files which needs
# to be placed on the host to run analysis, we have to keep the container
# running after the test is done. We can't map the volume into the test
# container because the tests are run with the `appuser` user and the volume
# will be owned by the host user. We can't map the current user into container,
# because all the testfiles are owned by appuser. So the only solution is to let
# the files be generated inside the container, and docker cp out onto the host.
coverage-withdocker:
	docker create --device=${HYPERVISOR_DEVICE} --name ${DOCKER_COVERAGE_NAME} ${TEST_IMG}:${IMAGE_VERSION} tail -f /dev/null
	docker start ${DOCKER_COVERAGE_NAME}
	docker exec ${DOCKER_COVERAGE_NAME} ${CONTAINER_COVERAGE_TEST_CMD} 
	docker cp ${DOCKER_COVERAGE_NAME}:${DOCKER_KM_TOP}/build/km/coverage/. ${TOP}/build/km/coverage/.
	docker rm --force ${DOCKER_COVERAGE_NAME}
	${DOCKER_RUN_BUILD} -v ${TOP}:${DOCKER_KM_TOP}:Z -w ${DOCKER_KM_TOP} $(BUILDENV_IMG) ${DOCKER_COVERAGE_CMD} 

K8S_TEST_COVERAGE_SCRIPT := ${TOP}/cloud/k8s/tests/k8s-run-coverage.sh
K8S_TEST_COVERAGE_IMAGE := ${TEST_K8S_IMAGE} 
K8S_TEST_COVERAGE_NAME := test-coverage-${PROCESSED_IMAGE_VERION}
coverage-withk8s:
	${K8S_TEST_COVERAGE_SCRIPT} ${K8S_TEST_COVERAGE_IMAGE} ${K8S_TEST_COVERAGE_NAME} "${CONTAINER_COVERAGE_TEST_CMD}" ${COVERAGE_KM_BLDDIR}
	${DOCKER_RUN_BUILD} -v ${TOP}:${DOCKER_KM_TOP}:Z -w ${DOCKER_KM_TOP} $(BUILDENV_IMG) ${DOCKER_COVERAGE_CMD} 

# install stuff for fedora per docker image info. Assume buildenv-image either built or pulled
# TODO: maybe make it generic to scan folders
buildenv-local-fedora:  ${KM_OPT_RT} ## make local build environment for KM
	@if ! docker image inspect ${BUILDENV_IMG} > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG} is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi
	sudo dnf install -y `docker history --format "{{ .CreatedBy }}" --no-trunc ${BUILDENV_IMG} | sed -rn '/dnf install/s/.*dnf install -y([^&]*)(.*)/\1/p'`
# Install stuff needed for buil, based on docker image info.
# Assume BUILDENV_IMG either built or pulled
buildenv-local-fedora: .buildenv-local-dnf .buildenv-local-lib ## make local build environment for KM

# Get a list of DNF packages from buildenv-image and install it on the host
.buildenv-local-dnf: .buildenv-local-check-image
	sudo dnf install -y `docker history --format "{{ .CreatedBy }}" --no-trunc ${BUILDENV_IMG}:$(BUILDENV_IMAGE_VERSION) | sed -rn '/dnf install/s/.*dnf install -y([^&]*)(.*)/\1/p'`

# Fetches alpine libs, and preps writeable 'runtime' dir.
# It'd a prerequisite for all further builds and needs to be called right after building
# or pull the buildenv-image. Call it via 'make buildenv-local-fedora' or 'make .buildenv-local-lib'
# so that libs are on the host and can be copied to runenv-image and testenv-image
.buildenv-local-lib: ${KM_OPT_RT} .buildenv-local-check-image
	docker create --name tmp_env $(BUILDENV_IMG):$(BUILDENV_IMAGE_VERSION)
	sudo docker cp tmp_env:/opt/kontain /opt
	docker rm tmp_env

.buildenv-local-check-image:
	@if ! docker image inspect ${BUILDENV_IMG}:$(BUILDENV_IMAGE_VERSION) > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG}:$(BUILDENV_IMAGE_VERSION) is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi

${KM_OPT_RT}: |
	sudo sh -c "mkdir -p $@ && chgrp users $@ && chmod 777 $@"

.PHONY: all clean test help gdb coverage covclean
