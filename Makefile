#
# Basic build for kontain machine (km) and related tests/examples.
# Subject to change - code is currently PoC
#
# dependencies:
#  elfutils-libelf-devel and elfutils-libelf-devel-static for gelf.h and related libs
#  OR:
#  docker - for 'make withdocker TARGET=<target>
#
# ====
#  Copyright © 2018-2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

# scan all these and  'make' stuff there
SUBDIRS := km runtime tests payloads

# build VMM and runtime library before trying to build tests
tests: km runtime

TOP := $(shell git rev-parse --show-cdup)

# On mac $(MAKE) evaluates to '/Applications/Xcode.app/Contents/Developer/usr/bin/make'
ifeq ($(shell uname), Darwin)
MAKE := make
endif
include ${TOP}make/actions.mk
