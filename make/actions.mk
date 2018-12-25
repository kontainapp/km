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
# TOP has to be defined before inclusion
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

include ${TOP}/make/custom.mk

# path from ${TOP} to this dir
FROMTOP = $(patsubst $(realpath ${TOP})/%,%, $(realpath .)/)
# dir where to put the build artifacts
BLDDIR = $(addprefix ${TOP}/build/, ${FROMTOP})
CFLAGS = ${COPTS} -Wall -ggdb $(patsubst %,-I %,${INCLUDES})
DEPS = $(addprefix ${BLDDIR}/,${SOURCES:%.c=%.d})
OBJS = $(addprefix ${BLDDIR}/,${SOURCES:.c=.o})
BLDEXEC = $(addprefix ${BLDDIR}/,${EXEC})
BLDLIB = $(addprefix ${BLDDIR}/lib,$(addsuffix .a,${LIB}))

ifneq (${SUBDIRS},)

all: subdirs ## Build all in all subdirs - basically, build + test recursively

subdirs: $(SUBDIRS)
clean: subdirs  ## clean all build artifacts. 
test: subdirs   ## build all and run tests everywhere

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: subdirs $(SUBDIRS)

else # not SUBDIRS, i.e. BLDEXEC or BLDLIB

ifneq (${BLDEXEC},)

all: ${BLDEXEC}
${BLDEXEC}: $(OBJS)
	$(CC) $(OBJS) $(addprefix -l ,${LLIBS}) -o $@

endif

ifneq (${BLDLIB},)

all: ${BLDLIB}
${BLDLIB}: $(OBJS)
	rm -f $@; ar cr $@ $(OBJS)

endif

${OBJS} ${DEPS}: | ${BLDDIR}	# order only prerequisite - just make sure it exists

${BLDDIR}:
	mkdir -p $@

${BLDDIR}/%.o: %.c
	$(CC) -c ${CFLAGS} $< -o $@

# note ${BLDDIR} in the .d file - this is what tells make to get .o from ${BLDDIR}
#
${BLDDIR}/%.d: %.c
	set -e; rm -f $@; \
	$(CC) -MM ${CFLAGS} $< | sed 's,\($*\)\.o[ :]*,${BLDDIR}/\1.o $@ : ,g' > $@;

clean:
	rm -rf ${BLDDIR}

#
# do not generate .d file if target is clean
#
ifneq ($(MAKECMDGOALS), clean)
-include ${DEPS}
endif # ($(MAKECMDGOALS), clean)

endif # (${SUBDIRS},)

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
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
    /^[.a-zA-Z0-9_-]+:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo 'For specific help in folders, try "(cd <dir>; make help)"'
	@echo ""

# dockerized build 
# 
DLOC := ${TOP}/docker/build
DIMG := km-buildenv
DFILE ?= Dockerfile

# run dockerized build
# TBD: separate build from run so --privileged is not needed for build
withdocker: ## Run dockerized make. Use TARGET to change what's built, i.e. "make withdocker TARGET=clean [DFILE=Dockerfile.ubuntu]"
	docker build -t $(DIMG) $(DLOC) -f ${DLOC}/${DFILE}
	docker run --privileged --rm -v $(realpath ${TOP}):/src -w /src/${FROMTOP} $(DIMG) $(MAKE) $(MAKEFLAGS) $(TARGET)

.PHONY: all clean test help withdocker
