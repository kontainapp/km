#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

CFLAGS = -Wall -ggdb $(patsubst %,-I %,${INCLUDES}) -O2
DEPS = ${SRC:%.c=%.d}
OBJ = ${SRC:.c=.o}

TOP := ..
INCLUDES := ${TOP}/include
SRC := load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c

all: km
km: $(OBJ)
	$(CC) $(OBJ) -lelf -o km

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM ${CFLAGS} $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@;

clean:
	rm -f *.o km *.d

test: all

help:
	
#
# do not generate .d file if target is clean
#
ifneq ($(MAKECMDGOALS), clean)
-include ${DEPS}
endif # ($(MAKECMDGOALS), clean)

.PHONY: all clean test help
