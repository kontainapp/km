#  Copyright © 2018-2019 Kontain Inc. All rights reserved.
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

include ${TOP}make/locations.mk

default: all

all: dltest.km

dltest.km: dlmain.o lib_bundle.o
	kontain-gcc -o $@ $+ -Wl,-u__km_dllist -Wl,-u__km_ndllist

lib_bundle.o: dllib1.o dllib2.o
	kontain-bundlelib -o $@ $+

clean:
	rm -f *.o *.so dltest

test:
