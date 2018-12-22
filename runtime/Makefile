#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

# -ffunction-sections -fdata-sections work together with linker script, see km.ld
# Other -f... options are to reduce generated code size

CFLAGS = -Wall -ggdb $(patsubst %,-I %,${INCLUDES}) -Os \
	-fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-fexcess-precision=standard -frounding-math -fno-tree-loop-distribute-patterns -fno-stack-protector
DEPS = ${SRC:%.c=%.d}
OBJ = ${SRC:.c=.o}

TOP := ..
INCLUDES := ${TOP}/include
SRC := global.c read.c write.c exit.c accept.c bind.c listen.c socket.c getsockopt.c setsockopt.c \
	   strlen.c puts.c

.PHONY: all clean test help
all: libruntime.a
libruntime.a: $(OBJ)
	ar cr $@ $(OBJ)

$(OBJ):

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM ${CFLAGS} $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@;

clean:
	rm -f *.o *.a *.d

test: all

help:

#
# do not generate .d file if target is clean
#
ifneq ($(MAKECMDGOALS), clean)
-include ${DEPS}
endif # ($(MAKECMDGOALS), clean)
