# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Makefile for building and packing Python payload runtime for KM

TOP := $(shell git rev-parse --show-cdup)
VERS ?= v3.7.4
SHELL=/bin/bash

# Location of cpython sources. When building in container, it is set in buildenv dockerfile.
PYTHONTOP ?= cpython

# component id for docker images
COMPONENT := python

# this is how we run python,km tests in container
CONTAINER_TEST_CMD := ./scripts/test-run.sh ./python

# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := cpython/python.km

# name to be added to label(s)
PAYLOAD_NAME := Python3.7

# a list to be passed to tar to be packed into container image
PAYLOAD_FILES := --exclude='*/test/*' --exclude='*/__pycache__/*' --exclude '*.exe' --exclude '*.whl' \
	cpython/python.km cpython/Lib cpython/Modules/Setup scripts

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
PYTHON_DISTRO_FILES := $(addprefix cpython/, Lib Modules build pybuilddir.txt dlstatic_km.mk)

# 'yes' if cpython was build from src (based on existable of .git repo)
FROMSRC=$(shell test -d $(PYTHONTOP)/.git && echo yes)

# full path to top of the git repo
PATH_TOP=$(realpath ${TOP})

# Python build artifacts are prepared by either
#   'fromsrc' in which case there is full cpython .git repo and we do full cpython build locally
# or
#   'buildenv-image' in which case python is build in docker, and we get 'blank' container with precompiled python libs
#
# build fromsrc and from buildenv-image are mutually exclusive. Default is from image. To switch, run 'make clobber' and then
# either 'make fromsrc' or just 'make' (which will )
#
# Depending on the above, we run 'all' target commands either inside of container (`make in-blank-container` is run there) or in the build tree
#

# Compile built-in extensions (if needed) and link python.km
export define link_cpython_km
	${MAKE} -C cpython -f dlstatic_km.mk   # compile built-in modules
	./link-km.sh ${PYTHONTOP} cpython "${EXTRA_LINKFILES}" "${CUSTOM_KM}"
endef

all:
ifneq ($(FROMSRC),yes)
	${DOCKER_RUN} -v $(PATH_TOP):$(PATH_TOP):Z -w $(PATH_TOP) ${BUILDENV_IMG}:${BUILDENV_IMAGE_VERSION} \
			make -C payloads/python in-blank-container \
				EXTRA_LINKFILES="${EXTRA_LINKFILES}"  CUSTOM_KM="${CUSTOM_KM}"
else
	 eval $(link_cpython_km)
endif

# Copy python artifacts to the outside, and link there with in-source-tree runtime
in-blank-container: ## invoked in blank container by ``make all''. DO NOT invoke manually
	tar -C ${PYTHONTOP}/.. -cf - ${PYTHON_DISTRO_FILES} | tar -xf -
	cd cpython && patch -p1 < ../unittest.patch
	eval $(link_cpython_km)

fromsrc: ## Clean build cpython from source. Use 'make clobber' to prepare for this one
	git clone https://github.com/python/cpython.git -b ${VERS}
	cd cpython && ./configure && make -j`expr 2 \* $(nproc)` | tee bear.out
	cd cpython && patch -p1 < ../unittest.patch
	eval $(prepare_builtins)
	eval $(link_cpython_km)

# Build custom python km
custom: all ## build python-custom.km. Optionally accepts CUSTOM_NAME=config_name (default 'custom', using extensions/python-custom.json)

# CUSTOM_NAME=xxx (default 'custom') user-settable name of custom configuration
CUSTOM_NAME ?= custom
# stuff deducted from CUSTOM_NAME:
CUSTOM_CONFIG ?= ./extensions/python-$(CUSTOM_NAME).json
CUSTOM_MODULES ?= $(shell jq -r .modules[].name ${CUSTOM_CONFIG})

ifeq (${MAKECMDGOALS},custom)
# these 2 vars need to be defined only for target 'custom' as they impact what is being linked
EXTRA_LINKFILES = $(foreach m,${CUSTOM_MODULES},@Modules/$m/linkline_km.txt)
CUSTOM_KM = python-${CUSTOM_NAME}.km
MODULES = ${CUSTOM_MODULES}

all: .check_custom_config pull-modules
.PHONY: .check_custom_config
.check_custom_config:
	@if [ ! -f ${CUSTOM_CONFIG} ] ; then echo "***WARNING @{CUSTOM_CONFIG} is not found"; fi # make sure we fail if custom config is not there
endif

# rebuild builtin extensions
define prepare_builtins
	cd cpython && ../extensions/prepare_extension.py bear.out --skip ../extensions/skip_builtins.txt
endef
builtins: ## helper target to rebuilt builtin extension in clone cpython dir. Only works in fromsrc mode
	eval $(prepare_builtins)

# MODULES can be used for alist of modules. Default is all validated ones from modules.json
build-modules push-modules pull-modules pack-modules: ## Operations on Kontain artifacts for dynamically linked modules
	./extensions/build_modules.sh --$(subst -modules,,$@) "${MODULES}"

test-modules: ## Basic test pass - rebuilds python, rebuilds and pushes all modules, and does custom link
	./extensions/test.sh
	@echo TODO: run test over custom KM, including new modules test

login: ## personal helper for token-based login.
	cat  ~/.docker/token  | docker login -u $(shell cat ~/.docker/username) --password-stdin

clean:
	rm -rf $(PYTHONTOP)/*.km $(PYTHONTOP)/python $(foreach m,${ALL_MODULES},$(PYTHONTOP)/Modules/$m)
	images=`docker image ls -q  'kpython/*'` ; if [ -n "$$images" ] ; then docker rmi $$images ; fi
	@echo -e "${GREEN}Note: use 'make clobber' to clean up cpython build.${NOCOLOR}"

test test-all: ${PAYLOAD_KM} ${TEST_KM}
	scripts/test-run.sh ${KM_BIN} ${PAYLOAD_KM}

test-all-withdocker: test-withdocker # alias for test-withdocker

# If something needs to be copied/modified before building runenv image in Docker,
# define this function
# TODO (fix it): runenv-image for python only works with ENTRYPOINT override: --entrypoint /python
# Also it's HUGE (170MB), need stuff stripped
export define runenv_prep
	tar -cf - ${PYTHON_DISTRO_FILES} scripts | tar -C $(RUNENV_PATH) -xf -
	echo '#!/km --putenv=PYTHONPATH=/cpython/Lib' > $(RUNENV_PATH)/python ; chmod a+x $(RUNENV_PATH)/python
endef
clobber: ## Clobber cpython build by removing cpython dir
	rm -rf cpython

clobber-modules:
	cd cpython; git clean -xdff Lib Modules

include ${TOP}make/images.mk
