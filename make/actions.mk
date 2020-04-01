# Copyright © 2018-2019 Kontain Inc. All rights reserved.
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
#  		TOP := $(shell git rev-parse --show-toplevel)
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

# all locations/file names
include ${TOP}/make/locations.mk
# customization of build should be in custom.mk
include ${TOP}/make/custom.mk

CFLAGS = ${COPTS} ${LOCAL_COPTS} -Wall -ggdb3 -pthread $(addprefix -I , ${INCLUDES})
DEPS = $(addprefix ${BLDDIR}/, $(addsuffix .d, $(basename ${SOURCES})))
OBJS = $(sort $(addprefix ${BLDDIR}/, $(addsuffix .o, $(basename ${SOURCES}))))
BLDEXEC = $(addprefix ${BLDDIR}/,${EXEC})
BLDLIB = $(addprefix ${BLDDIR}/lib,$(addsuffix .a,${LIB}))

ifneq (${SUBDIRS},)

all: subdirs ## Build all in all subdirs - basically, build + test recursively

# Note: one target per line, so 'make help' works
subdirs: $(SUBDIRS)
clean: subdirs ## clean all build artifacts.
test: subdirs ## run basic tests (KM tests and short payload tests)
test-all: subdirs ## run extended tests(KM tests full payload tests)
coverage: subdirs ## build and run tests with code coverage support
covclean: subdirs ## clean coverage-related build artifacts
buildenv-image: subdirs ## builds and packages all build environment image
push-buildenv-image: subdirs ## Push buildenv images to a cloud registry. PROTECTED OPERATION.
pull-buildenv-image: subdirs ## Pulls buildenv images from a cloud registry
testenv-image: subdirs ## builds and packages testable image
push-testenv-image: subdirs ## Push test images to a cloud registry. IMAGE_VERSION should provide image tag name
pull-testenv-image: subdirs ## Pulls test images from a cloud registry. IMAGE_VERSION is mandatory. Used mainly in CI
runenv-image: subdirs ## builds and packages image with runtime environment for each payload
push-runenv-image: subdirs ## Push runtime images to a cloud registry. IMAGE_VERSION can be used used to modify tag name
pull-runenv-image: subdirs ## Pulls runtime images from a cloud registry. IMAGE_VERSION an be used used to modify tag name
validate-runenv-image: subdirs ## Runs basic validation command for runtime image
runenv-demo-image: subdirs ## builds a demo image based off runenv-image
push-runenv-demo-image: subdirs ## push runtime demo images to cloud registry. IMAGE_VERSION can be used used to modify tag name
test-withdocker: subdirs ##
test-all-withdocker: subdirs ## build all and run KM and payload tests
test-withk8s: subdirs ## run tests using k8s
test-all-withk8s: subdirs ## run all tests (with long running) using k8s

$(SUBDIRS):
	$(MAKE) -C $@  MAKEFLAGS="$(MAKEFLAGS)" $(MAKECMDGOALS) MAKEOVERRIDES=

.PHONY: subdirs $(SUBDIRS)

else # not SUBDIRS, i.e. EXEC or LIB

ifneq (${EXEC},)

all: ${BLDEXEC}
${BLDEXEC}: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDOPTS) $(LOCAL_LDOPTS) $(addprefix -l ,${LLIBS}) -o $@

# if VERSION_SRC is defined, force-rebuild these sources on 'git info' changes
ifneq (${VERSION_SRC},)
${VERSION_SRC}: ${TOP}/.git/HEAD ${TOP}/.git/index
	touch $@

${BLDDIR}/$(subst .c,.o,${VERSION_SRC}): CFLAGS += -DSRC_BRANCH='"${SRC_BRANCH}"' -DSRC_VERSION='"${SRC_VERSION}"' -DBUILD_TIME='"${BUILD_TIME}"'
endif

endif

ifneq (${LIB},)

all: ${BLDLIB}
${BLDLIB}: $(OBJS)
	@echo "Making " $@; rm -f $@; ${AR} crs $@ $(OBJS)

endif

.PHONY: coverage covclean .cov_clean
ifneq (${COVERAGE},)
coverage:
	$(MAKE) BLDTYPE=$(COV_BLDTYPE) MAKEFLAGS="$(MAKEFLAGS)" COPTS="$(COPTS) --coverage"

covclean:
	$(MAKE) BLDTYPE=$(COV_BLDTYPE) MAKEFLAGS="$(MAKEFLAGS)" .cov_clean

.cov_clean:
	rm -rf ${BLDDIR}
else
coverage: all

covclean:
	@echo `pwd`: Nothing to do for '$@'.
endif

OBJDIRS = $(sort $(dir ${OBJS}))
${OBJS} ${DEPS}: | ${OBJDIRS}	# order only prerequisite - just make sure it exists

${OBJDIRS}:
	mkdir -p $@

# The sed regexp below is the same as in Visual Studion Code problemWatcher in tasks.json.
# \\1 - filename :\\2:\\3: - position (note \\3: is optional) \\4 - severity \\5 - message
# The sed transformation adds ${FROMTOP} prefix to file names to facilitate looking for files
#
${BLDDIR}/%.o: %.c
	@echo $(CC) -c ${CFLAGS} $< -o $@
	@$(CC) -c ${CFLAGS} $< -o $@ |& \
		sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="

${BLDDIR}/%.o: %.s
	@echo $(CC) -c ${CFLAGS} $< -o $@
	@$(CC) -c ${CFLAGS} $< -o $@ |& \
		sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="

${BLDDIR}/%.o: %.S
	@echo $(CC) -c ${CFLAGS} $< -o $@
	@$(CC) -c ${CFLAGS} $< -o $@ |& \
		sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="

# note ${BLDDIR} in the .d file - this is what tells make to get .o from ${BLDDIR}
#
${BLDDIR}/%.d: %.c
	@echo $(CC) -MT ${BLDDIR}/$*.o -MT $@ -MM ${CFLAGS} $< -o $@
	@set -e; rm -f $@; $(CC) -MT ${BLDDIR}/$*.o -MT $@ -MM ${CFLAGS} $< -o $@ |& \
		sed -r -e "s=^(.*?):([0-9]+):([0-9]+)?:?\\s+(note|warning|error|fatal error):\\s+(.*)$$=${FROMTOP}&="

test test-all: all

clean:
	rm -rf ${BLDDIR}

#
# do not generate .d file for some targets
#
$(shell [[ "${MAKECMDGOALS}" =~ ^${NO_DEPS_TARGETS}$$ || "${MAKEFLAGS}" =~ "n" ]] )
ifneq ($(.SHELLSTATUS),0)
-include ${DEPS}
endif

.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'

endif # (${SUBDIRS},)

# Support for 'make withdocker'
#
# Usage:
#	make withdocker [TARGET=<make-target>] [DTYPE=<os-type>]
#  	<make-target> is "all" by default (same as in regular make, with no docker)
#  	<os-type> is "fedora" by default. See ${TOP}/docker/build for supported OSes
#
__testing := $(strip $(findstring test,$(TARGET)) $(findstring coverage,$(TARGET)))

# this is needed for proper image name forming
COMPONENT := km
withdocker: ## Build using Docker container for build environment. 'make withdocker [TARGET=clean] [DTYPE=ubuntu]'
	@if ! docker image ls --format "{{.Repository}}:{{.Tag}}" | grep -q ${BUILDENV_IMG} ; then \
		echo -e "$(CYAN)${BUILDENV_IMG} is missing locally, will try to pull from registry. Use 'make buildenv-image' to build$(NOCOLOR)" ; fi
ifneq ($(__testing),)  # only add --device=/dev/kvm is we are testing
	${DOCKER_RUN_TEST} -v $(realpath ${TOP}):/src:Z -w /src/${FROMTOP} $(BUILDENV_IMG) $(MAKE) MAKEFLAGS="$(MAKEFLAGS)" $(TARGET)
else
	${DOCKER_RUN} -v $(realpath ${TOP}):/src:Z -w /src/${FROMTOP} $(BUILDENV_IMG) $(MAKE) MAKEFLAGS="$(MAKEFLAGS)" $(TARGET)
endif

.PHONY: all clean test help withdocker
