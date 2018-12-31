# Copyright © 2018 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# To be included throughout the KONTAIN project
# TOP has to be defined before inclusion, usually as 
#  		TOP := $(shell git rev-parse --show-cdup)
#
# There are three possible builds depending on the including Makefile,
# depending which of the following is defined:
# - SUBDIRS: recurse into the subdirs
# - EXEC: build the executable
# - LIB: build the library
#
# - SOURCES: source files to build from
# - INCLUDES: directories to search for include files
# - LLIBS: to link with -l ${LLIBS}
#
SHELL=/bin/bash

# make sure the value is non-empty - if we are 
# already at the top, we can get empty line from upstairs
# (Note that below we'll expect the trailing '/' in directories)
ifeq ($(strip ${TOP}),)
TOP := ./
endif

# this is the path from the TOP to current dir
FROMTOP := $(shell git rev-parse --show-prefix)
# all build results (including obj etc..)  go under this one
BLDTOP := ${TOP}build/
# build for the current run goes here 
BLDDIR := ${BLDTOP}${FROMTOP}

# customization of build should be in custom.mk
include ${TOP}make/custom.mk

CFLAGS = ${COPTS} -Wall -ggdb $(patsubst %,-I %,${INCLUDES})
DEPS = $(addprefix ${BLDDIR},${SOURCES:%.c=%.d})
OBJS = $(addprefix ${BLDDIR},${SOURCES:.c=.o})
BLDEXEC = $(addprefix ${BLDDIR},${EXEC})
BLDLIB = $(addprefix ${BLDDIR}lib,$(addsuffix .a,${LIB}))

ifneq (${SUBDIRS},)

all: subdirs ## Build all in all subdirs - basically, build + test recursively

subdirs: $(SUBDIRS)
clean: subdirs  ## clean all build artifacts. 
test: subdirs   ## build all and run tests everywhere

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: subdirs $(SUBDIRS)

else # not SUBDIRS, i.e. EXEC or LIB

ifneq (${EXEC},)

all: ${BLDEXEC}
${BLDEXEC}: $(OBJS)
	$(CC) $(OBJS) $(addprefix -l ,${LLIBS}) -o $@

endif

ifneq (${LIB},)

all: ${BLDLIB}
${BLDLIB}: $(OBJS)
	rm -f $@; ${AR} crs $@ $(OBJS)

endif

OBJDIRS := $(sort $(dir ${OBJS}))
${OBJS} ${DEPS}: | ${OBJDIRS}	# order only prerequisite - just make sure it exists

${OBJDIRS}:
	mkdir -p $@

# The sed regexp below is the same as in problemWatcher in tasks.json.
# \\1 - filename :\\2:\\3: - position (note \\3: is optional) \\4 - severity \\5 - message
# The sed transformation adds ${FROMTOP} prefix to file names to facilitate looking for files
#
${BLDDIR}%.o: %.c
	@echo $(CC) -c ${CFLAGS} $< -o $@
	@$(CC) -c ${CFLAGS} $< -o $@ |& \
	sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="

# note ${BLDDIR} in the .d file - this is what tells make to get .o from ${BLDDIR}
#
${BLDDIR}%.d: %.c
	@echo $(CC) -MT ${BLDDIR}$*.o -MT $@ -MM ${CFLAGS} $< -o $@
	@set -e; rm -f $@; \
	$(CC) -MT ${BLDDIR}$*.o -MT $@ -MM ${CFLAGS} $< -o $@ |& \
	sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="

test: all

clean:
	rm -rf ${BLDDIR}

#
# do not generate .d file if target is clean
#
ifneq ($(MAKECMDGOALS), clean)
-include ${DEPS}
endif # ($(MAKECMDGOALS), clean)

endif # (${SUBDIRS},)

# Generic support - applies for all flavors (SUBDIR, EXEC, LIB, whatever) 

# Support for "make help"
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
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
    /^[.a-zA-Z0-9_-]+:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo 'For specific help in folders, try "(cd <dir>; make help)"'
	@echo ""

# dockerized build 
#
# use Dockerfile-fedora, Dockerfile-ubuntu, etc... to get different flavors 
DTYPE ?= fedora
DLOC := ${TOP}docker/build
DIMG := km-buildenv-${DTYPE}
DFILE := Dockerfile.$(DTYPE)

# Support for 'make withdocker'
#
# Usage: 
#	make withdocker [TARGET=<make-target>] [DTYPE=<os-type>]
#  	<make-target> is "all" by default (same as in regular make, with no docker)
#  	<os-type> is "fedora" by default. See ${TOP}/docker/build for supported OSes
#
# TBD:
#  - separate build from run so --privileged is not needed for build
#
#  Note:
# 'chmod' in the last line is needed to enable 'make clean' to work without docker
#    (since docker-produced files are owned by the container user, which is root by default)
#
withdocker: ## Build in docker. 'make withdocker [TARGET=clean] [DTYPE=ubuntu]'
	docker build -t ${DIMG} ${DLOC} -f ${DLOC}/${DFILE}
	docker run --privileged --rm -v $(realpath ${TOP}):/src -w /src/${FROMTOP} $(DIMG) $(MAKE) $(MAKEFLAGS) $(TARGET)
	docker run --rm -v $(realpath ${TOP}):/src ${DIMG} chmod -fR a+w /src/build

# Support for simple debug print (make debugvars)
VARS_TO_PRINT ?= TOP FROMTOP BLDTOP BLDDIR SUBDIRS CFLAGS BLDEXEC BLDLIB DEPS OBJS
debugvars:   ## prints interesting vars and their values
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))

.PHONY: all clean test help withdocker debugvars
