#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

CFLAGS = -Wall -ggdb -O2
LDLAGS = --gc-sections --print-gc-sections -T $(TOP)/km.ld $(patsubst %,-L %,${LDDIRS}) $(patsubst %,-l%,${LIBS})
DEPS = ${SRC:%.c=%.d}
OBJ = ${SRC:.c=.o}

TOP := ..
LDDIRS := ${TOP}/runtime
LIBS := runtime
SRC := hello.c hello_html.c

KM := ${TOP}/km/km

EXECS := hello.km hello_html.km hello hello_html

.PHONY: all clean test help
all: test
test: ${EXECS} ## Builds all and runs a simple test
	# TBD - make return from km compliant with "success" :-)
	-${KM} hello.km
	-./hello
	${KM} hello_html.km &
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	@echo "Success"	
	./hello_html &
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	@echo "Success"	

$(EXECS):
$(OBJ):

hello.km:	hello.o
	ld hello.o -o hello.km ${LDLAGS}

hello_html.km: hello_html.o
	ld hello_html.o -o hello_html.km ${LDLAGS}

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
