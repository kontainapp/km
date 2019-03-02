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
SRC = hello_test.c hello_html_test.c brk_test.c mmap_test.c gdb_test.c exit_value_test.c \
		load_test.c  hello_loop_test.c hello_2_loops_test.c memslot_test.c mutex_test.c futex_test.c

COPTS ?= -ggdb -O2
CFLAGS = -Wall $(COPTS) -D_FORTIFY_SOURCE=0 -fno-stack-protector -pthread -I$(TOP)include -I$(TOP)km
KM_LDFLAGS = --gc-sections --print-gc-sections -T ${TOP}km.ld $(patsubst %,-L %,${LDDIRS}) $(patsubst %,-l%,${LIBS})
LOADLIBES ?= -ggdb -static -pthread
DEPS = ${SRC:%.c=%.d}
OBJ = ${SRC:%.c=%.o}
LDDIRS := ${TOP}build/runtime
LIBS := runtime
EXECS := ${SRC:%.c=%.km} ${SRC:%.c=%}
BATS := bats/bin/bats
TEST_FILES := km_core_tests.bats
MATCH ?= ".*"

all: ${EXECS} ## Build all test, for Linux and KM

# Run all or some of tests (may need /dev/kvm or privileged container)
test: all  ## Run all or some of tests. Use 'MATCH=<string>' to limit the set by name
	@${BATS} -p -f "${MATCH}" ${TEST_FILES}

%.km: %.o
	${LD} $< -o $@ ${KM_LDFLAGS}

.PHONY: load_expected_size
load_expected_size: load_test.km
	@nm load_test.km | awk '/__end_elf__/{ print "0x" $$1}'

%.d: %.c
	@# print the command we really need to run
	@echo $(CC) -MT ${BLDDIR}$*.o -MT $@ -MM ${CFLAGS} $< -o $@
	@# run the command and adjust it's error messages so VS Code navigates properly
	@set -e; rm -f $@; \
	  $(CC) -MT ${BLDDIR}$*.o -MT $@ -MM ${CFLAGS} $< -o $@ |& \
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

.PHONY: all clean test help gdb
