#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# Makefile for building and packing Python payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

# Git tag to fetch.
TAG ?= v3.9.7

# Version (used in python paths) and ABI flags (used in *some* paths)
VERS = $(shell echo ${TAG} | sed -e 's/^v//' -e 's/.[0-9]*$$//')
# in Python 3.8+ ABI flag 'm' is no longer used, see https://bugs.python.org/issue36707.
ABI = $(shell if [ $$(echo ${VERS} | sed 's/^[0-9]*.//') -le 7 ] ; then echo m; fi)

BUILDENV_IMAGE_EXTRA_ARGS = --label "VERS=${VERS}" --build-arg=VERS=${VERS} --build-arg=ABI=${ABI} --build-arg=TAG=${TAG}
TESTENV_IMAGE_EXTRA_ARGS  = --label "VERS=${VERS}" --build-arg=VERS=${VERS} --build-arg=ABI=${ABI}
RUNENV_IMAGE_EXTRA_ARGS   = --label "VERS=${VERS}" --build-arg=VERS=${VERS}

# Location of cpython sources. When building in container, it is set in buildenv dockerfile.
PYTHONTOP ?= cpython

# Component id for docker images.
# Note that for now it is version-independent (i.e. not python${VERS})
# to avoid version dependencies downstream in FROM/demo images etc.
COMPONENT := python

# this is how we run python.km tests in container
CONTAINER_TEST_CMD := ./scripts/test-run.sh ./python

# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := cpython/python.km

# name to be added to label(s)
PAYLOAD_NAME := ${COMPONENT}

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
PYTHON_DISTRO_FILES := $(addprefix cpython/, Lib Modules build pybuilddir.txt dlstatic_km.mk dlstatic_km.mk.json python.km.symbols.c)

# 'yes' if cpython was build from src (based on existable of .git repo)
FROMSRC=$(shell test -d $(PYTHONTOP)/.git && echo yes)

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
	${CC} -I${TOP}/runtime -c -o cpython/python.km.symbols.o cpython/python.km.symbols.c
	./extensions/linkline.py cpython/dlstatic_km.mk.json $$(test -f ./${CUSTOM_TAKE} && echo -n --take ${CUSTOM_TAKE})
	for mod in $(foreach m,${CUSTOM_MODULES},cpython/Modules/$m/dlstatic_km.mk.json) ; do \
		./extensions/linkline.py $${mod} $$(test -f ./${CUSTOM_TAKE} && echo -n --take ${CUSTOM_TAKE}) ; \
	done
	./link-km.sh ${PYTHONTOP} cpython "$(foreach m,${CUSTOM_MODULES},@Modules/$m/linkline_km.txt)" "${CUSTOM_KM}"
endef

# Set info for runenv-image build

RUNENV_ROOTFS_SCRIPT := runenv/rootfs.sh
RUNENV_VALIDATE_CMD := ("python3" "scripts/sanity_test.py")

export define runenv_prep
	${RUNENV_ROOTFS_SCRIPT} ${PYTHONTOP} ${RUNENV_PATH} ${VERS} $(notdir ${PAYLOAD_KM})
endef

include ${TOP}/make/images.mk

all:
ifneq ($(FROMSRC),yes)
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:${TOP}:z \
		${KM_OPT_VOLUME} \
		-w ${TOP}/payloads/python \
		${BUILDENV_IMG_TAGGED} \
		make in-blank-container CUSTOM_MODULES="${CUSTOM_MODULES}" CUSTOM_KM="${CUSTOM_KM}"
else
	eval $(link_cpython_km)
endif

# Copy python artifacts to the outside, and link there with in-source-tree runtime
in-blank-container: ## invoked in blank container by ``make all''. DO NOT invoke manually
	tar -C ${PYTHONTOP}/.. -cf - ${PYTHON_DISTRO_FILES} | tar -xf -
	eval $(link_cpython_km)

${PYTHONTOP}: |
	git clone https://github.com/python/cpython.git -b ${TAG}

${PYTHONTOP}/config.status: | ${PYTHONTOP}
	cp km_api.c cpython/Modules/km_api.c
	grep "kontain km_api.c" cpython/Modules/Setup.local || echo "kontain km_api.c" >> cpython/Modules/Setup.local
	cd ${PYTHONTOP} && ./configure

fromsrc: ${PYTHONTOP}/config.status ## Clean build cpython from source. Use 'make clobber' to prepare for this one
	cd ${PYTHONTOP} && make -j`expr 2 \* $$(nproc)` | tee bear.out
	eval $(prepare_builtins)
	eval $(link_cpython_km)

# Build custom python km
custom: all ## build python-custom.km. Optionally accepts CUSTOM_NAME=config_name (default 'custom', using extensions/python-custom.json)

ifeq (${MAKECMDGOALS},custom)
# these vars need to be defined only for target 'custom' as they impact what is being linked
# CUSTOM_NAME=xxx (default 'custom') user-settable name of custom configuration
CUSTOM_NAME ?= custom
CUSTOM_CONFIG ?= ./extensions/python-$(CUSTOM_NAME).json
CUSTOM_MODULES ?= $(shell jq -r .modules[].name ${CUSTOM_CONFIG} | sed -e 's-^null$$--')
CUSTOM_TAKE ?= ./extensions/take_modules-$(CUSTOM_NAME).txt
CUSTOM_KM = python-${CUSTOM_NAME}.km

all: .check_custom_config pull-modules
.PHONY: .check_custom_config
.check_custom_config:
	@if [ ! -f ${CUSTOM_CONFIG} ] ; then echo "***WARNING ${CUSTOM_CONFIG} is not found"; fi # make sure we fail if custom config is not there
endif

# rebuild builtin extensions
define prepare_builtins
	cd cpython \
		&& mv python python.orig \
		&& ../extensions/prepare_extension.py python.orig --log=quiet --self \
		&& ../extensions/prepare_extension.py bear.out --skip ../extensions/skip_builtins.txt
endef
builtins: ## helper target to rebuilt builtin extension in clone cpython dir. Only works in fromsrc mode
	eval $(prepare_builtins)

# MODULES can be used for a list of modules. Default is all validated ones from modules.json
build-modules push-modules pull-modules pack-modules: ## Operations on Kontain artifacts for dynamically linked modules
	./extensions/build_modules.sh --$(subst -modules,,$@) "${CUSTOM_MODULES}"

test-modules: ## Basic test pass - rebuilds python, rebuilds and pushes all modules, and does custom link
	./extensions/test.sh
	@echo TODO: run test over custom KM, including new modules test

clean:
	rm -rf $(PYTHONTOP)/*.km{,d}{,.mimalloc} ./python $(PYTHONTOP)/python{,.orig} $(foreach m,${ALL_MODULES},$(PYTHONTOP)/Modules/$m)
	rm -rf $(RUNENV_PATH)
	@echo -e "${GREEN}Note: use 'make clobber' to clean up cpython build.${NOCOLOR}"

cleansrc: clean
	make -C ${PYTHONTOP} distclean

# test locally same way as in container
test test-all: ${PAYLOAD_KM} ${TEST_KM}
	ln -sf cpython/python .
	${CONTAINER_TEST_CMD}

clobber: ## Clobber cpython build by removing cpython dir
	rm -rf cpython

clobber-modules:
	cd cpython; git clean -xdff Lib Modules

RUNENV_TAG := python python-${VERS}
