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
COPTS ?= -ggdb -O2
CFLAGS = -Wall $(COPTS) -fno-stack-protector -pthread -I$(TOP)include -I$(TOP)km
KM_LDFLAGS = --gc-sections --print-gc-sections -T ${TOP}km.ld $(patsubst %,-L %,${LDDIRS}) $(patsubst %,-l%,${LIBS})
LOADLIBES ?= -ggdb -static -pthread
DEPS = ${SRC:%.c=%.d}
OBJ = ${SRC:%.c=%.o}

# Tests to run in KM only, build only .km
SRC_KM_ONLY := exit_value.c load.c
# Tests can run on linux and km, and compare. Build for both.
SRC_LINUX := hello.c hello_html.c mem_brk.c mmap_test.c gdb_test.c
# Helpers, build for KM and linux, do not run as part of test
SRC_HELPERS := hello_loop.c hello_2_loops.c  memslot.c

SRC = ${SRC_LINUX} ${SRC_KM_ONLY} ${SRC_HELPERS}
LDDIRS := ${TOP}build/runtime
LIBS := runtime
KM := ${TOP}build/km/km
EXECS := ${SRC:%.c=%.km} ${SRC_LINUX:%.c=%} ${SRC_HELPERS:%.c=%}
# add a leading '.'
TEST_TARGETS :=  ${SRC_KM_ONLY:%.c=.%} ${SRC_LINUX:%.c=.%}

all: ${EXECS} ## Build a simple test

# Run tests in regular bash and in km/VMM.
test: all $(TEST_TARGETS) ## Run a simple test (may need /dev/kvm or privileged container)

.exit_value:
	@echo === Test: $@ ===
	pl_status=`${KM} exit_value.km |& awk '/status/ { print $$13 }'` ; \
			if [ "$$pl_status" != "0x11" ] ; then echo ==$$pl_status ; false; fi
	@echo -e "$@: ${SUCCESS}"

.load:
	@echo === Test: $@ ===
	@echo "${KM} load.km |& awk /status/ '{ print \$$13 }'"
	@pl_status=`${KM} load.km |& awk '/status/ { print $$13 }'` ; \
			if [ "$$pl_status" != "0x0" ] ; then echo ==$$pl_status ; \
			echo Failed - try to run \'make load_expected_size\' in tests, and replace load.c:size value ; false; fi
	@echo -e "$@: ${SUCCESS}"

.hello:
	@echo === Test: $@ ===
	./hello > 1.out
	${KM} hello.km > 2.out
	@diff 1.out 2.out || false
	@echo -e "$@: ${SUCCESS}"

.mem_brk:
	@echo === Test: $@ ===
	${KM} mem_brk.km || false
	@echo -e "$@: ${SUCCESS}"

.hello_html:
	@echo === Test: $@ ===
	./hello_html &
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	${KM} hello_html.km &
	@# 'km' monitor may start slow (while we are experimenting with startup time)
	@# let it time to start before running curl
	sleep 0.5s
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	@echo -e "$@: ${SUCCESS}"
	@rm 1.out 2.out

.mmap_test:
	@echo === Test: $@ ===
	@echo ${KM} mmap_test.km
	@if ${KM} mmap_test.km ; then echo -e "$@: ${SUCCESS}"; else false ; fi

.gdb_test:
	@echo === Test: $@ ===
	@$(KM)  -g 3333 gdb_test.km  &
	@sleep 0.5
	@gdb 	-q -nx --ex="target remote :3333"  \
			--ex="source cmd_for_test.gdb"  --ex=c --ex=q \
			gdb_test.km | grep -q SUCCESS
	@echo -e "$@:  ${SUCCESS}"


%.km: %.o
	${LD} $< -o $@ ${KM_LDFLAGS}

.PHONY: load_expected_size
load_expected_size: load.km
	@nm load.km | awk '/__end_elf__/{ print "0x" $$1}'

%.d: %.c
	@echo $(CC) -MT ${BLDDIR}$*.o -MT $@ -MM ${CFLAGS} $< -o $@
	@set -e; rm -f $@; \
	$(CC) -MT ${BLDDIR}$*.o -MT $@ -MM ${CFLAGS} $< -o $@ |& \
	sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="

clean:
	rm -f *.o *.d $(EXECS) 1.out 2.out

help:

SUCCESS := \033[32;1mSuccess\033[0m

#
# do not generate .d file if target is clean
#
ifneq ($(MAKECMDGOALS), clean)
-include ${DEPS}
endif # ($(MAKECMDGOALS), clean)

.PHONY: all clean test help gdb
