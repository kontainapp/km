#
# Basic build for kontain machine (km) and related tests/examples.
# Subject to change - code is currently PoC
#
# dependencies:
#  elfutils-libelf-devel  and elfutils-libelf-devel-static for gelf.h and related libs
#  OR:
#  docker - for 'make withdocker TARGET=<target>
#
# ====
#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

SUBDIRS := km runtime tests

.PHONY: all subdirs $(SUBDIRS) clean test help docker
all: subdirs ## Build all in all subdirs - basically, build + test recursively

subdirs: $(SUBDIRS)
clean: subdirs  ## clean all build artifacts. 
test: subdirs   ## just runs pre-build tests everywhere

tests: km runtime
$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)


# dockerized build 
# 
DLOC := docker/build
DIMG := km-buildenv

# run dockerized build
# TBD: separate build from run so --privileged is not needed for build
withdocker: ## Run dockerized make. Use TARGET to change what's built, i.e. "make withdocker TARGET=clean"
	docker build -t $(DIMG) $(DLOC)
	docker run --privileged --rm -v `pwd`:/src -w /src $(DIMG) $(MAKE) $(MAKEFLAGS) $(TARGET)


# support for "make help"
#
# colors for nice color in output
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
CYAN := \033[36m
NOCOLOR := \033[0m

#
# 'Help' target - based on '##' comments in targets
#
# This target ("help") scans Makefile for '##' in targets and prints a summary
# Note - used awk to print (instead of echo) so escaping/coloring is platform independed
.PHONY: help
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
    /^[.a-zA-Z0-9_-]+:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo 'For specific help in folders, try "(cd <dir>; make help)"'
