#  Copyright © 2018 Kontain Inc. All rights reserved.
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

default: all

include ${TOP}make/locations.mk

SRC = hello_test.c hello_html_test.c brk_test.c mmap_test.c gdb_test.c exit_value_test.c hc_test.c \
		load_test.c  hello_loop_test.c hello_2_loops_test.c memslot_test.c mutex_test.c futex_test.c \
		getline_test.c mem_test.c exit_grp_test.c intr_test.c brk_map_test.c crash_test.c cpuid_test.c \
		longjmp_test.c cpuid_print_details_test.c stray_test.c signal_test.c

SRC_CPP = var_storage_test.cpp

COPTS ?= -ggdb -O2
CFLAGS = -Wall $(COPTS) -D_FORTIFY_SOURCE=0 -fno-stack-protector -pthread -I$(TOP)include -I$(TOP)km
LDFLAGS = -ggdb -static -pthread
KM_LDFLAGS = -Wl,-u__km_handle_interrupt -Wl,-e__start_c__ -Wl,--gc-sections -nostdlib -T ${TOP}gcc.ld $(patsubst %,-L %,${LDDIRS}) $(patsubst %,-l%,${LIBS})
LDLIBS = -lstdc++ -lgcc_eh
LDDIRS := ${TOP}build/runtime
DEPS = ${SRC:%.c=%.d} ${SRC_CPP:%.cpp=%.d}
OBJ = ${SRC:%.c=%.o} ${SRC_CPP:%.cpp=%.o}
LIBS := runtime
EXECS := ${SRC:%.c=%} ${SRC:%.c=%.km} ${SRC_CPP:%.cpp=%} ${SRC_CPP:%.cpp=%.km}

CXXFLAGS=${COPTS}

# Test: run by BATS, definition in TEST_FILES, test name has to MATCH
BATS := time bats/bin/bats
TEST_FILES := km_core_tests.bats
MATCH ?= ".*"

all: ${EXECS} ## Build all test, for Linux and KM

# Run all or some of tests (may need /dev/kvm or privileged container)
# ignore test failure when building coverage
test: all  ## Run all or some of tests. Use 'MATCH=<string>' to limit the set by name
ifeq (${MAKECMDGOALS},.coverage)
	@-export KM_BIN=$(KM_BIN); ${BATS} -p -f "${MATCH}" ${TEST_FILES}
else
	@export KM_BIN=$(KM_BIN); ${BATS} -p -f "${MATCH}" ${TEST_FILES}
endif

%.km: %.o
	${CC} ${LDFLAGS} $< -o $@ ${LDLIBS} ${KM_LDFLAGS} && chmod a-x $@

.PHONY: load_expected_size
load_expected_size: load_test.km
	@nm load_test.km | awk '/__end_elf__/{ print "0x" $$1}'

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
	@echo $(CPP) -MT $*.o -MT $@ -MM ${CFLAGS} ${CPPFLAGS} $< -o $@
	@# run the command and adjust it's error messages so VS Code navigates properly
	@set -e; rm -f $@; \
	  $(CPP) -MT $*.o -MT $@ -MM ${CFLAGS} ${CPPFLAGS} $< -o $@ |& \
	  sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="
	@# make sure dependencies are rebuilt if needed.
	@# sed strips everything before colon, removes leading and trailing spaces, then splits words into individual lines
	@# and trailing ':'. See - http://scottmcpeak.com/autodepend/autodepend.html for explanation
	@sed -e 's/^.*: *//' -e 's/ *\\$$//'  -e 's/^ *//' -e 's/ \+/:\n/g'  -e 's/$$/:/' $*.d | cat $*.d - > $*.d.tmp
	@mv $*.d.tmp $*.d

clean:
	rm -f *.o *.d $(EXECS)

#
# do not generate .d file if target is clean
#
ifneq ($(MAKECMDGOALS), clean)
-include ${DEPS}
endif # ($(MAKECMDGOALS), clean)

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
ifeq (${MATCH},)
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

endif

.PHONY: all clean test help gdb coverage covclean

distro distroclean publish publishclean:
	@echo $(notdir $(CURDIR)): nothing to do for target '$@'
