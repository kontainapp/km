#  Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

TOP := $(shell git rev-parse --show-toplevel)

LIB := kontain

SOLIB := kontain

SOURCES := libkontain.c

INCLUDES := ${TOP}/include

all: libkontain.h
	cp libkontain.h ${KM_OPT_INC}
	cp ${BLDTOP}/lib/libkontain/libkontain.a ${KM_OPT_LIB}
	cp ${BLDTOP}/lib/libkontain/libkontain.so ${KM_OPT_LIB}

include ${TOP}/make/actions.mk
