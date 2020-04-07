#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

# Ubuntu default is dash. We invoke this by make -C tests ... in CI which is Ubuntu
SHELL=/bin/bash
TOP := $(shell git rev-parse --show-toplevel)
COMPONENT := km

NO_RUNENV = true
include ${TOP}/make/images.mk

# this is how we run tests in containers:
CONTAINER_TEST_CMD :=  run_bats_tests.sh --km=/tests/km

SRC = hello_test.c hello_html_test.c brk_test.c mmap_test.c gdb_test.c exit_value_test.c hcallargs_test.c \
		load_test.c  hello_loop_test.c hello_2_loops_test.c memslot_test.c mutex_test.c futex_test.c \
		getline_test.c mem_test.c exit_grp_test.c intr_test.c brk_map_test.c crash_test.c cpuid_test.c \
		longjmp_test.c cpuid_print_details_test.c stray_test.c signal_test.c hello_2_loops_tls_test.c \
		regions_test.c mprotect_test.c pthread_cancel_test.c filesys_test.c filepath_test.c socket_test.c \
		dl_iterate_phdr_test.c env_test.c locale_test.c munmap_monitor_maps_test.c \
		gdb_server_entry_race_test.c gdb_qsupported_test.c gdb_delete_breakpoint_test.c gdb_nextstep_test.c \
		mmap_1_test.c madvise_test.c misc_test.c gdb_sharedlib2_test.c gdb_lots_of_threads_test.c \
		decode_test.c hcperf_test.c vdso_test.c auxv_test.c gdb_protected_mem_test.c clone_test.c \
		sigaltstack_test.c

SRC_CPP = var_storage_test.cpp throw_basic_test.cpp

HELPER_SRC = mmap_tester.c

SHRSRC := dlopen_test.c dlopen_test_lib.c dlopen_test_lib2.c

# Use existing small dir for buildenv image builds (saves on sending data to docker svc)
BUILDENV_PATH  := ./gdb
TESTENV_PATH := .

CFLAGS = -Wall $(COPTS) -ggdb3 -fPIC -fno-stack-protector -pthread -I${TOP}/include -I${TOP}/km
LDFLAGS = -ggdb3 -pthread -ldl
KCC := kontain-gcc
DEPS = ${SRC:%.c=%.d} ${SHRSRC:%.c=%.d} ${SRC_CPP:%.cpp=%.d}
KM_PAYLOADS := ${SRC:%.c=%.km} ${SRC_CPP:%.cpp=%.km}
EXECS := ${KM_PAYLOADS} ${KM_PAYLOADS:%.km=%}

SEXECS := ${SHRSRC:%.c=%.so} ${KM_PAYLOADS:%.km=%.so}
DEXECS := ${SRC:%.c=%.kmd} ${SRC_CPP:%.cpp=%.kmd}

CXXFLAGS=${CFLAGS}
${SRC_CPP:%.cpp=%.km}: KCC=kontain-g++
${SRC_CPP:%.cpp=%.kmd}: KCC=kontain-g++
${SRC_CPP:%.cpp=%.so}: KCC=kontain-g++
${SRC_CPP:%.cpp=%}: CC=g++

# Only tests matching this regexp will be run
MATCH ?= .*

all: ${EXECS} ${SEXECS} ${DEXECS} ## Build all test, for Linux and KM

${KM_PAYLOADS}: ${TOP}/build/runtime

# Run all or some of tests (needs /dev/kvm)
# ignore test failure when building coverage

ifneq ($(findstring coverage,${MAKECMDGOALS}),)
IGNORE_FAILURE := --ignore-failure
endif

libhelper.a: ${HELPER_SRC:%.c=%.o}
	ar rv $@ $<

$(EXECS) $(SEXECS) ${DEXECS}: libhelper.a
KM_LDFLAGS +=  -L. -lhelper

test: all  ## Run all or some of tests. Use 'MATCH=<string>' to limit the set by name
	./run_bats_tests.sh --km=${KM_BIN} --pretty ${IGNORE_FAILURE} --match="${MATCH}"

%.so: %.o
	${KCC} -shared ${LDFLAGS} $< -o $@ ${KM_LDFLAGS}

%.km: %.o
	${KCC} ${LDFLAGS} $< -o $@ ${KM_LDFLAGS}

%.kmd: %.o
	${KCC} ${LDFLAGS} -rdynamic $< -o $@ ${KM_LDFLAGS}

.PHONY: load_expected_size
load_expected_size: load_test.km load_test.so
	@nm load_test.km | awk '/ _end/{ print "0x" $$1}'
	@nm load_test.so | awk '/ _end/{ print "0x" $$1}'

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
	rm -f *.o *.d $(EXECS) $(SEXECS) ${DEXECS} kmcore libhelper.a

#
# do not generate .d file for some targets
#
$(shell [[ "${MAKECMDGOALS}" =~ ^${NO_DEPS_TARGETS}$$ || "${MAKEFLAGS}" =~ "n" ]] )
ifneq ($(.SHELLSTATUS),0)
-include ${DEPS}
endif

covclean: ## Clean up code coverage build artifacts
	$(MAKE) BLDTYPE=$(COV_BLDTYPE) MAKEFLAGS="$(MAKEFLAGS)" .cov_clean

coverage: all  ## Build with code coverage, then run tests and generate reports
	@$(MAKE) BLDTYPE=$(COV_BLDTYPE) MAKEFLAGS="$(MAKEFLAGS)" .coverage

ifeq (${BLDTYPE},${COV_BLDTYPE})
# we need this nesting (.cov* targets) to pass BLDTYPE and set env.
.PHONY: .coverage .cov_init .cov_clean
.cov_clean:
	rm -rf ${BLDDIR}

# extra config for Code coverage
COVERAGE_CMD_NAME := gcovr
ifeq (${MATCH},.*)
# Generate a fail if code coverage is too low - only for full test passes
COVERAGE_THRESHOLDS :=  --fail-under-branch 40  --fail-under-line 55
endif

# example: gcovr ../build/km/coverage/ -r ../km --html-details -o ../build/km/coverage/report/km.html
COVERAGE_REPORT := ${KM_BLDDIR}/report.html
COVERAGE_CMD_FLAGS  :=  -r ${TOP}/km --html --html-details -o ${COVERAGE_REPORT} ${KM_BLDDIR} \
		 --html-title "Kontain Monitor Code Coverage report" --print-summary -j 4 --exclude-unreachable-branches --delete

.coverage: test
	@echo -e "COVERAGE: ${GREEN}Analyzing the code coverage...${NOCOLOR}"
	${COVERAGE_CMD_NAME} ${COVERAGE_CMD_FLAGS} ${COVERAGE_LOCATIONS} ${COVERAGE_THRESHOLDS}
	@echo -e "COVERAGE: ${GREEN}Code Coverage Details: $(abspath $(COVERAGE_REPORT))${NOCOLOR}"


.cov_init:
	@if [ ! $$(command -v $(COVERAGE_CMD_NAME)) ]  ; then echo -e "${RED}'${COVERAGE_CMD_NAME} command is missing - please install ${COVERAGE_CMD_NAME} package (e.g. 'sudo dnf install ${COVERAGE_CMD}${NOCOLOR}"; false  ; fi
	@echo -e "COVERAGE: ${GREEN}Running tests with code coverage...${NOCOLOR}"

test: .cov_init
endif # ifeq (${BLDTYPE},$(COV_BLDTYPE))

test-all: test ## alias for test

# install stuff for fedora per docker image info. Assume buildenv-image either built or pulled
# TODO: maybe make it generic to scan folders
buildenv-local-fedora:  ${KM_OPT_RT} ## make local build environment for KM
	@if ! docker image inspect ${BUILDENV_IMG} > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG} is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi
	sudo dnf install -y `docker history --format "{{ .CreatedBy }}" --no-trunc ${BUILDENV_IMG} | sed -rn '/dnf install/s/.*dnf install -y([^&]*)(.*)/\1/p'`
	docker create --name tmp_env $(BUILDENV_IMG)
	sudo docker cp tmp_env:/opt/kontain /opt
	docker rm tmp_env

.PHONY: all clean test help gdb coverage covclean
