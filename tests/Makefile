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
CFLAGS = -Wall $(COPTS) -fno-stack-protector
LDLAGS = --gc-sections --print-gc-sections -T ${TOP}km.ld $(patsubst %,-L %,${LDDIRS}) $(patsubst %,-l%,${LIBS})
LOADLIBES ?= -static
DEPS = ${SRC:%.c=%.d}
OBJ = ${SRC:%.c=%.o}

SRC_KM_ONLY := load.c exit_value.c mem_brk.c gdb_test.c
SRC_LINUX := hello.c hello_html.c hello_loop.c
SRC = ${SRC_LINUX} ${SRC_KM_ONLY}
LDDIRS := ${TOP}build/runtime
LIBS := runtime
KM := ${TOP}build/km/km
EXECS := ${SRC:%.c=%.km} ${SRC_LINUX:%.c=%}

GDB_PAYLOAD := gdb_test.km
GDB_EXPECTED_VAL := 1219779

all: ${EXECS} ## Build a simple test

# Run tests in regular bash and in km/VMM.
test: all ## Run a simple test (may need /dev/kvm or privileged container)
	pl_status=`${KM} exit_value.km |& awk '/status/ { print $$11 }'` ; \
			if [ "$$pl_status" != "0x11" ] ; then echo ==$$pl_status ; false; fi
	@echo -e "Exit value: ${SUCCESS}"
	pl_status=`${KM} load.km |& awk '/status/ { print $$11 }'` ; \
			if [ "$$pl_status" != "0x0" ] ; then echo ==$$pl_status ; false; fi
	@echo -e "Load: ${SUCCESS}"
	./hello > 1.out
	${KM} hello.km > 2.out
	@diff 1.out 2.out || false
	@echo -e "Hello: ${SUCCESS}"
	${KM} mem_brk.km || false
	@echo -e "Mem_brk: ${SUCCESS}"
	./hello_html &
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	${KM} hello_html.km &
	@# 'km' monitor may start slow (while we are experimenting with startup time)
	@# let it time to start before running curl
	sleep 0.5s
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	@echo -e "Hello-html: ${SUCCESS}"
	@rm 1.out 2.out
	@# === GDB TEST ===
	@$(KM)  -g 3333 $(GDB_PAYLOAD)  &
	@sleep 0.5
	@gdb -q  \
			-ex "target remote localhost:3333" \
			-ex 'b print_str' \
			-ex c -ex n -ex n -ex n \
			-ex 'p var1' \
			-ex c -ex q $(GDB_PAYLOAD) | grep -q $(GDB_EXPECTED_VAL)
	@echo -e "gdb test:  ${SUCCESS}"

%.km: %.o
	${LD} $< -o $@ ${LDLAGS}

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
