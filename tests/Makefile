#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

TOP := $(shell git rev-parse --show-cdup)
CFLAGS = -Wall -ggdb -O2
LDLAGS = --gc-sections --print-gc-sections -T ${TOP}km.ld $(patsubst %,-L %,${LDDIRS}) $(patsubst %,-l%,${LIBS})
DEPS = ${SRC:%.c=%.d}
OBJ = ${SRC:.c=.o}

LDDIRS := ${TOP}build/runtime
LIBS := runtime
SRC := hello.c hello_html.c
KM := ${TOP}build/km/km
EXECS := hello.km hello_html.km hello hello_html

all: ${EXECS} ## Build a simple test

# Run tests in regular bash and in km/VMM.
# Note that 'hello' is supposed to return magic '17', or 0x11 (see tests/hello.c).
test: all ## Run a simple test (may need /dev/kvm or privileged container)
	./hello; if [ $$? -ne 17 ] ; then false; fi
	pl_status=`${KM} hello.km | awk '/status/ { print $$6 }'` ; \
			if [ "$$pl_status" != "0x11" ] ; then echo ==$$pl_status ; false; fi
	@echo "Hello: Success"
	./hello_html &
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	${KM} hello_html.km &
	# 'km' monitor may start slow (while we are experimenting with startup time)
	# let it time to start before running curl
	sleep 0.5s
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	@echo "Hello-html: Success"

hello.km:	hello.o
	${LD} hello.o -o hello.km ${LDLAGS}

hello_html.km: hello_html.o
	${LD} hello_html.o -o hello_html.km ${LDLAGS}

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM ${CFLAGS} $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@;

clean:
	rm -f *.o *.d $(EXECS)

help:

#
# do not generate .d file if target is clean
#
ifneq ($(MAKECMDGOALS), clean)
-include ${DEPS}
endif # ($(MAKECMDGOALS), clean)

.PHONY: all clean test help
