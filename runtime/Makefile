#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

SOURCES := global.c read.c write.c exit.c accept.c bind.c listen.c socket.c getsockopt.c \
		   setsockopt.c strlen.c puts.c
LIB := runtime

# -ffunction-sections -fdata-sections work together with linker script, see km.ld
# Other -f... options are to reduce generated code size
COPTS := -Os \
	-fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -fexcess-precision=standard -frounding-math \
	-fno-tree-loop-distribute-patterns -fno-stack-protector

TOP := $(shell git rev-parse --show-cdup)
include ${TOP}make/actions.mk

test: all
