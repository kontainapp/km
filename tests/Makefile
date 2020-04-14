#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

# Ubuntu default is "dash". We invoke this by make -C tests ... in CI which is Ubuntu
SHELL=/bin/bash

TOP := $(shell git rev-parse --show-toplevel)
CURRENT_DIR := ${TOP}/tests
COMPONENT := km

NO_RUNENV = true
# Building image here so we can pick up alpine libs via prep_alpine_libs.sh during 'buidenv_prep'
BUILDENV_PATH = ${KM_OPT}

# Prep alpine and kontain libs before docker build
export define buildenv_prep
	${CURRENT_DIR}/prep_alpine_libs.sh
endef

# unfortunately buildenv_prep a few vars have to be defined BEFORE this include, and much of the rest AFTER
# this is due to 'ifdef` in the image.mk processed by Make on file read.
# so it' stuck here in the middle of the file by design
include ${TOP}/make/images.mk

# this is how we run tests in containers:
CONTAINER_TEST_CMD :=  run_bats_tests.sh --km=/tests/km

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

# install stuff for fedora per docker image info. Assume buildenv-image either built or pulled
buildenv-local-fedora:  ${KM_OPT_RT} ## make local build environment for KM
	@if ! docker image inspect ${BUILDENV_IMG} > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG} is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi
	sudo dnf install -y `docker history --format "{{ .CreatedBy }}" --no-trunc ${BUILDENV_IMG} | sed -rn '/dnf install/s/.*dnf install -y([^&]*)(.*)/\1/p'`
	docker create --name tmp_env $(BUILDENV_IMG)
	sudo docker cp tmp_env:/opt/kontain /opt
	docker rm tmp_env

.PHONY: all clean test help gdb coverage covclean
