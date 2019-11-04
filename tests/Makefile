#  Copyright © 2018-2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
SHELL=/bin/bash
TOP := $(shell git rev-parse --show-cdup)

COMPONENT := km

SRC = hello_test.c hello_html_test.c brk_test.c mmap_test.c gdb_test.c exit_value_test.c \
		load_test.c  hello_loop_test.c hello_2_loops_test.c memslot_test.c mutex_test.c futex_test.c \
		getline_test.c mem_test.c exit_grp_test.c intr_test.c brk_map_test.c crash_test.c cpuid_test.c \
		longjmp_test.c cpuid_print_details_test.c stray_test.c signal_test.c hello_2_loops_tls_test.c \
		regions_test.c mprotect_test.c pthread_cancel_test.c filesys_test.c filepath_test.c socket_test.c \
		dl_iterate_phdr_test.c env_test.c locale_test.c

SRC_CPP = var_storage_test.cpp throw_basic_test.cpp

# Use existing small dir for buildenv image builds (saves on sending data to docker svc)
BUILDENV_PATH  := ./gdb

COPTS ?= -ggdb -O2
CFLAGS = -Wall $(COPTS) -fno-stack-protector -pthread -I$(TOP)include -I$(TOP)km
LDFLAGS = -ggdb -pthread
KCC := kontain-gcc
DEPS = ${SRC:%.c=%.d} ${SRC_CPP:%.cpp=%.d}
KM_PAYLOADS := ${SRC:%.c=%.km} ${SRC_CPP:%.cpp=%.km}
EXECS := ${KM_PAYLOADS} ${KM_PAYLOADS:%.km=%}

CXXFLAGS=${CFLAGS}
${SRC_CPP:%.cpp=%.km}: KCC=kontain-g++
${SRC_CPP:%.cpp=%}: CC=g++

# Test: run by BATS, definition in TEST_FILES, test name has to MATCH
BATS := time bats/bin/bats
TEST_FILES := km_core_tests.bats
MATCH ?= .*

# file for keeping per-test 'time' info
TIME_INFO = ${TOP}build/time_info.txt

# default target, should be first in file
all: ${EXECS} ## Build all test, for Linux and KM

${KM_PAYLOADS}: ${TOP}build/runtime

# Run all or some of tests (may need /dev/kvm or privileged container)
# ignore test failure when building coverage
test: all  ## Run all or some of tests. Use 'MATCH=<string>' to limit the set by name
	@rm -f ${TIME_INFO}
ifeq (${MAKECMDGOALS},.coverage)
	@-export KM_BIN=$(KM_BIN); export TIME_INFO=${TIME_INFO}; ${BATS} -p -f "${MATCH}" ${TEST_FILES}
else
	@export KM_BIN=$(KM_BIN); export TIME_INFO=${TIME_INFO};  ${BATS} -p -f "${MATCH}" ${TEST_FILES}
endif
	@echo -e "${GREEN}Tests slower than 0.1 sec:${NOCOLOR}"
	@grep elapsed ${TIME_INFO} | grep -v "elapsed 0:00.[01]" | sort -r
	@echo ""
	@rm -f ${TIME_INFO}

%.km: %.o
	${KCC} ${LDFLAGS} $< -o $@ ${KM_LDFLAGS}

.PHONY: load_expected_size
load_expected_size: load_test.km
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
	rm -f *.o *.d $(EXECS) kmcore $(TIME_INFO)

#
# do not generate .d file for some targets
#
$(shell [[ "${MAKECMDGOALS}" =~ ^(clean|clobber|.*-image|print-.*|debugvars)$$ || "${MAKEFLAGS}" =~ "n" ]] )
ifneq ($(.SHELLSTATUS),0)
-include ${DEPS}
endif

covclean: ## Clean up code coverage build artifacts
	$(MAKE) BLDTYPE=$(COV_BLDTYPE)/ MAKEFLAGS="$(MAKEFLAGS)" .cov_clean

coverage: all  ## Build with code coverage, then run tests and generate reports
	@$(MAKE) BLDTYPE=$(COV_BLDTYPE)/ MAKEFLAGS="$(MAKEFLAGS)" .coverage

ifeq (${BLDTYPE},$(COV_BLDTYPE)/)
# we need this nesting to pass COV_BLDTYPE and set env. properly
.PHONY: .coverage .cov_init .cov_clean
.cov_clean:
	rm -rf ${BLDDIR}

# extra config for Code coverate
COVERAGE_CMD_NAME := gcovr
ifeq (${MATCH},.*)
# Generate a fail if code coverage is too low - only for full test passes
COVERAGE_THRESHOLDS :=  --fail-under-branch 40  --fail-under-line 55
endif
# example: gcovr ../build/km/coverage/ -r ../km --html-details -o ../build/km/coverage/report/km.html
COVERAGE_CMD_FLAGS  :=  -r ${TOP}km --html --html-details -o ${COVERAGE_REPORT} ${KM_BLDDIR} \
		 --html-title "Kontain Monitor Code Coverage report" --print-summary -j 4 --exclude-unreachable-branches --delete

.coverage: test
	@echo -e "COVERAGE: ${GREEN}Analyzing the code coverage...${NOCOLOR}"
	@${COVERAGE_CMD_NAME} ${COVERAGE_CMD_FLAGS} ${COVERAGE_LOCATIONS} ${COVERAGE_THRESHOLDS}
	@echo -e "COVERAGE: ${GREEN}Code Coverage Details: $(abspath $(COVERAGE_REPORT))${NOCOLOR}"

test: .cov_init
.cov_init:
	@if [ ! $$(command -v $(COVERAGE_CMD_NAME)) ]  ; then echo -e "${RED}'${COVERAGE_CMD_NAME} command is missing - please install ${COVERAGE_CMD_NAME} package (e.g. 'sudo dnf install ${COVERAGE_CMD}${NOCOLOR}"; false  ; fi
	@echo -e "COVERAGE: ${GREEN}Running tests with code coverage...${NOCOLOR}"

endif # ifeq (${BLDTYPE},$(COV_BLDTYPE)/)

test-all: test

# Run full suite of tests regarless of being asked to run full or abbreviated set
# NOTE: if needed, we can run a shorter set by passing MATCH env with regexp, e.g.
#			${DOCKER_RUN_TEST} --env=MATCH='(exit|setup|hello)' ${TEST_IMG}:${IMAGE_VERSION} run_bats_tests.sh
test-withdocker test-all-withdocker:
	${DOCKER_RUN_TEST} ${TEST_IMG}:${IMAGE_VERSION} run_bats_tests.sh

# install stuff for fedora per docker image info. Assume buildenv-image either built or pulled
# TODO: maybe make it generic to scan folders
buildenv-local-fedora:  ## make local build environment for KM
	@if ! docker image inspect ${BUILDENV_IMG} > /dev/null ; then \
		echo -e "$(RED)${BUILDENV_IMG} is not available. Use 'make buildenv-image' or 'make pull-buildenv-image' to build or pull$(NOCOLOR)"; false; fi
	sudo dnf install -y `docker run --rm ${BUILDENV_IMG} dnf list --installed | awk '/x86/ {print $$1}' `
	docker create --name tmp_env $(BUILDENV_IMG)
	sudo docker cp tmp_env:/opt/kontain /opt
	docker rm tmp_env

# Info for making runenv-image and validate-runenv-image targets
#
# A little hackery here. PAYLOAD_KM is generally things like node.km or python.km, so we can call 'km payload_km'
# for validation we usually use RUNENV_VALIDATE_CMD (e.g. hello.js) so overall command looks like 'km node.km hello.js'
# Hower for KM Only, we want to run 'km validat_cmd.km' right away, so we set PAYLOAD_KM to validation script.
# Note that we support runenv and validate-runenv here for consistency only, the real value is in payloads/ dir.
PAYLOAD_KM := hello_test.km
PAYLOAD_NAME := KontainMonitor
# make sure validation script is copied to runenv path before building docker image
export define runenv_prep
	cp ${PAYLOAD_KM} $(RUNENV_PATH)
endef

.PHONY: all clean test help gdb coverage covclean

distroclean publish publishclean:
	@echo $(notdir $(CURDIR)): nothing to do for target '$@'

include ${TOP}make/images.mk
