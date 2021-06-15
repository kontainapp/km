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
	${MAKE} -j -C ${TOP}/km coverage
	cp ${KM_BIN} ${TESTENV_PATH}
	cp ${KM_LDSO} ${TESTENV_PATH}
	cp ${COVERAGE_KM_BIN} ${TESTENV_PATH}/km.coverage
endef

override define testenv_cleanup =
	rm ./km
	rm ./libc.so
	rm km.coverage
endef

# To serialiaze, pass TEST_JOBS=1
ifneq ($(TEST_JOBS),.)
  __BATS_JOBS := --jobs=${TEST_JOBS}
endif

# Base command to run BATS, and derivatives - how to run outside of container, in container, etc..
BATS_CMD := run_bats_tests.sh --match="${MATCH}" --usevirt="${USEVIRT}" ${IGNORE_FAILURE} ${__BATS_JOBS}
# this is how we run in outside of container
TEST_CMD := time ./${BATS_CMD} --km=${KM_BIN} --pretty
COVERAGE_TEST_CMD := time ./${BATS_CMD} --km=${COVERAGE_KM_BIN} --pretty
# this is how we run tests in containers:
CONTAINER_TEST_CMD          := ${DOCKER_KM_TOP}/tests/${BATS_CMD} --km=${KM_OPT_BIN}/km
CONTAINER_COVERAGE_TEST_CMD := ${DOCKER_KM_TOP}/tests/${BATS_CMD} --km=${KM_OPT_COVERAGE_BIN}/km

# Build from these sources. Note that test may be skipped using "skip" support in out bats files
SRC_C := $(abspath $(wildcard *_test.c))
SRC_CPP := $(abspath $(wildcard *_test.cpp))
HELPER_SRC := $(abspath $(wildcard *_helper.c))

# Test driver and shared libs for KM dlopen test, plus extra stuff we need to buid .so from
SRC_SHR := $(abspath dlopen_test.c dlopen_test_lib.c dlopen_test_lib2.c)

DEBUG ?= -ggdb3
CFLAGS = -Wall -Werror $(COPTS) ${DEBUG}  \
         -DGREATEST_STDOUT=stderr \
			-fPIC -fno-stack-protector -pthread \
			-I${TOP}/include -I${TOP}/km -ffile-prefix-map=${CURDIR}/=
CXXFLAGS=${CFLAGS}

LDLIBS  :=  -pthread -ldl -L. -lhelper
LDFLAGS := ${DEBUG}

DEPS = ${SRC_C:%.c=%.d} ${SRC_SHR:%.c=%.d} ${SRC_CPP:%.cpp=%.d} ${HELPER_SRC:%.c=%.d}

SUFFIXES := .km .kmd .km.so .alpine.km .alpine.kmd .${DTYPE}
TESTS := ${SRC_C:%.c=%} ${SRC_CPP:%.cpp=%}
EXECS := ${TESTS} $(abspath hello_test_link) \
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
	${TEST_CMD}

test-all: test ## alias for test

KCC := kontain-gcc
KCXX := kontain-g++

# use g++ link libs when sources were C++
# ${SRC_CPP:%.cpp=%}: CC=${CXX}
${SRC_CPP:%.cpp=%.fedora}: CC=${CXX} -static
${SRC_C:%.c=%.fedora}: LDLIBS += -static
${SRC_CPP:%.cpp=%.km}: KCC=${KCXX}
${SRC_CPP:%.cpp=%.kmd}: KCC=${KCXX}
${SRC_CPP:%.cpp=%.alpine.km}:KCC=${KCXX}
${SRC_CPP:%.cpp=%.alpine.kmd}: KCC=${KCXX}
${SRC_CPP:%.cpp=%.km.so}: KCC=${KCXX}

%.${DTYPE}: %.o
	${CC} ${LDFLAGS} $< ${LDLIBS} -o $@

%_test: ${KM_BIN}
	ln -sf $$(realpath --relative-to=${CURDIR} $<) $@ ; touch $@

%_test_link: %_test
	ln -sf $$(realpath --no-symlinks --relative-to=${CURDIR} $<) $@

%.so: %.o
	${CC} -shared ${LDFLAGS} $< ${LDLIBS} -o $@

%.km: %.o
	${KCC} -static ${LDFLAGS} $< ${LDLIBS} -o $@

%.kmd: %.o
	${KCC} -dynamic ${LDFLAGS} $< ${LDLIBS} -o $@

%.alpine.km: %.o
	${KCC} -static -alpine ${LDFLAGS} $< ${LDLIBS} -o $@

%.alpine.kmd: %.o
	${KCC} -dynamic -alpine ${LDFLAGS} $< ${LDLIBS} -o $@

%.km.so: %.o
	${KCC} -shared ${LDFLAGS} $< -o $@ ${LDLIBS}

.PHONY: load_expected_size
load_expected_size: load_test.km # load_test.so
	@nm load_test.km | awk '/ _end/{ print "0x" $$1}'

%.d: %.c
	$(CC) -MT $*.o -MT $@ -MM ${CFLAGS} $< -o $@

%.d: %.cpp
	$(CXX) -MT $*.o -MT $@ -MM ${CXXFLAGS} ${CPPFLAGS} $< -o $@

clean:
	rm -f *.o *.d ${EXECS} ${SEXECS} kmcore km km.coverage *.a *.so *.km *.kmd *_test *.${DTYPE}

#
# do not generate .d file for some targets
#
no_deps := $(shell [[ "${MAKECMDGOALS}" =~ ^${NO_DEPS_TARGETS}$$ || "${MAKEFLAGS}" =~ "n" ]] && echo -n match)
ifneq ($(no_deps),match)
-include ${DEPS}
endif

COVERAGE_SCRIPT := ${CURDIR}/scripts/coverage.sh
COVERAGE_REPORT_TITLE := ${IMAGE_VERSION}
test-coverage:
	${COVERAGE_TEST_CMD}
	${COVERAGE_SCRIPT} ${TOP}/km ${COVERAGE_KM_BLDDIR} ${COVERAGE_KM_BLDDIR} ${COVERAGE_REPORT_VERSION}

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
	-docker rm --force ${DOCKER_COVERAGE_NAME}
	docker create --device=${HYPERVISOR_DEVICE} --name ${DOCKER_COVERAGE_NAME} ${TEST_IMG_TAGGED} tail -f /dev/null
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

UPLOAD_COVERAGE_SCRIPT := ${TOP}/cloud/k8s/tests/upload-coverage.sh
upload-coverage: .check_vars
	${UPLOAD_COVERAGE_SCRIPT} ${COVERAGE_KM_BLDDIR} ${PROCESSED_IMAGE_VERION} ${GITHUB_TOKEN}

MANUAL_UPLOAD_REPORT_NAME := manual-${USER}-${PROCESSED_IMAGE_VERION}
upload-coverage-manual: .check_vars
	${UPLOAD_COVERAGE_SCRIPT} ${COVERAGE_KM_BLDDIR} ${MANUAL_UPLOAD_REPORT_NAME} ${GITHUB_TOKEN}

.PHONY: all clean test help gdb test-coverage
