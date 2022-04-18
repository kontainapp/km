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
# A helper include. Used to include some vars in dirs which cannot include
# actions.mk (e.g. tests which need their own compile / link flags)

default: all

ifeq (${TOP},)
  $(error "TOP needs to be set before including this mk file ")
endif

# this is the path from the TOP to current dir. Note this has a trailing /
FROMTOP := $(shell git rev-parse --show-prefix)

# Current branch. Note that on Github merges the branch name is in
# GITHUB_HEAD_REF, and current branch is refs/merge/pr-id.
# In all other cases we extract branch name from git rev-parse
# on GITHUB PullRequests (and push)
ifneq (${GITHUB_HEAD_REF},)
SRC_BRANCH ?= ${GITHUB_HEAD_REF}
else
SRC_BRANCH ?= $(shell git rev-parse --abbrev-ref HEAD)
ifeq (${SRC_BRANCH},HEAD)
SRC_BRANCH = ${GITHUB_SHA}
endif
endif
# current SHA, to be saved for 'km -v' uniquiness
SRC_SHA ?= $(shell git rev-parse HEAD)

PATH := $(abspath ${TOP}/tools/bin):${PATH}

# sha and build time for further reporting
SRC_VERSION := $(shell git rev-parse HEAD)
BUILD_TIME := $(shell date -Iminutes)

# all build results (including obj etc..)  go under this one
BLDTOP := ${TOP}/build

# Build results go here.
# For different build types (e.g. coverage), pass BLDTYPE=<type>, e.g BLDTYPE=coverage
BLDDIR := $(abspath ${BLDTOP}/${FROMTOP}/${BLDTYPE})

# km location needs to be fixed no matter what is the FROMTOP,
# so we can use KM from different places
KM_BLDDIR := $(abspath ${BLDTOP}/km/${BLDTYPE})
KM_BIN := ${KM_BLDDIR}/km
KM_RT := ${BLDTOP}/runtime
KM_OPT := /opt/kontain
KM_OPT_BIN := ${KM_OPT}/bin
KM_OPT_INC := ${KM_OPT}/include
KM_OPT_LIB := ${KM_OPT}/lib
KM_OPT_RT := ${KM_OPT}/runtime
KM_OPT_ALPINELIB := ${KM_OPT}/alpine-lib
KM_LDSO := ${BLDTOP}/runtime/libc.so

GETENFORCE := /usr/sbin/getenforce

# Build with code coverage if BLDTYPE set to this.
COV_BLDTYPE := coverage
COVERAGE_KM_BLDDIR := ${BLDTOP}/km/${COV_BLDTYPE}
COVERAGE_KM_BIN := ${COVERAGE_KM_BLDDIR}/km
KM_OPT_COVERAGE := ${KM_OPT}/${COV_BLDTYPE}
KM_OPT_COVERAGE_BIN := ${KM_OPT_COVERAGE}/bin
KM_OPT_COVERAGE_BIN_KM := ${KM_OPT_COVERAGE_BIN}/km

# dockerized build
# TODO: Some of these values should be moved to images.mk , but we have multiple
# dependencies on that , so keeping it here for now
#
# use DTYPE=fedora or DTYPE=ubuntu, etc... to get different flavors
DTYPE ?= fedora
USER  ?= appuser

# needed in 'make withdocker' so duplicating it here, for now
BUILDENV_IMG  ?= kontainapp/buildenv-${COMPONENT}-${DTYPE}

DOCKER_BUILD_LABEL := \
	--label "Vendor=Kontain.app" \
	--label "Version=0.8" \
	--label "Description=${PAYLOAD_NAME} in Kontain" \
	--label "KONTAIN:BRANCH=$(SRC_BRANCH)" \
	--label "KONTAIN:SHA=$(SRC_SHA)"

DOCKER_BUILD := docker build ${DOCKER_BUILD_LABEL}

CURRENT_UID := $(shell id -u)
CURRENT_GID := $(shell id -g)

# Default hypervisor to kvm.
HYPERVISOR_DEVICE ?= /dev/kvm
# Use DOCKER_RUN_CLEANUP="" if container is needed after a run
DOCKER_RUN_CLEANUP ?= --rm
# When running tests in containers on CI, we can't use tty and interactive
DOCKER_INTERACTIVE ?= -it

DOCKER_KRUN_RUNTIME ?= --runtime krun
DOCKER_RUN := docker run --sysctl net.ipv4.ip_unprivileged_port_start=1024 ${DOCKER_RUN_CLEANUP}
# DOCKER_RUN_BUILD are used for building and other operations that requires
# output of files to volumes. When we need to write files to the volumes mapped
# in, we need to map the current used into the container, since containers are
# using `appuser`, which is different from user on the host.
DOCKER_RUN_BUILD := ${DOCKER_RUN} -u ${CURRENT_UID}:${CURRENT_GID}
DOCKER_RUN_TEST := ${DOCKER_RUN} ${DOCKER_INTERACTIVE} --device=${HYPERVISOR_DEVICE} --init

# These PODMAN_* variables mirror the docker related ones.
# Initially we use these to verify that runenv-images work with podman
PODMAN_KRUN_RUNTIME ?= --runtime krun
PODMAN_RUN := podman run ${DOCKER_RUN_CLEANUP}
PODMAN_RUN_TEST := ${PODMAN_RUN} ${DOCKER_INTERACTIVE} --init

# Inside docker image (buildenv + testenv), appuser will be the user created
# inside the container.
DOCKER_HOME_PATH := /home/appuser
DOCKER_KM_TOP := ${DOCKER_HOME_PATH}/km
DOCKER_BLDTOP := ${DOCKER_KM_TOP}/build
DOCKER_COVERAGE_KM_BLDDIR := ${DOCKER_BLDTOP}/km/coverage

# These volumes needs to be mapped into runenv images. At the moment, they
# contain km and km linkers.
ifeq (${BLDTYPE}, ${COV_BLDTYPE})
	KM_OPT_BIN_PATH := ${KM_OPT_COVERAGE_BIN}
else
	KM_OPT_BIN_PATH := ${KM_OPT_BIN}
endif

KM_OPT_KM := ${KM_OPT_BIN_PATH}/km
KM_KM_VOLUME := -v ${KM_OPT_KM}:${KM_OPT_KM}:z
KM_OPT_VOLUME := -v ${KM_OPT_BIN}:${KM_OPT_BIN}:z -v ${KM_OPT_RT}:${KM_OPT_RT}:z -v ${KM_OPT_LIB}:${KM_OPT_LIB}:z

# Utility functions for common docker operations.
clean_container = @-docker rm --force ${1} 2>/dev/null
clean_container_image = @-docker rmi -f ${1} 2>/dev/null

# cloud-related stuff. By default set to Azure
#
# name of the cloud, as well as subdir of $(TOP)/cloud where the proper scripts hide. Use CLOUD='' to build with no cloud
# All cloud stuff can be turned off by passing CLOUD=''
CLOUD ?= azure

ifneq ($(CLOUD),)
# now bring all cloud-specific stuff needed in forms on 'key = value'
CLOUD_SCRIPTS := $(TOP)/cloud/$(CLOUD)
include $(CLOUD_SCRIPTS)/cloud_config.mk
endif

# Use current branch as image version (tag) for docker images.
IMAGE_VERSION ?= latest
BUILDENV_IMAGE_VERSION ?= latest

# Release tag for artifacts default. Real value should be set in release.yaml workflow, or at the very least in environment
RELEASE_TAG ?= v0.1-test

# Generic support - applies for all flavors (SUBDIR, EXEC, LIB, whatever)

# regexp for targets which should not try to build dependencies (.d)
NO_DEPS_TARGETS := (clean|clobber|.*-image|.*-release|\.buildenv-local-.*|buildenv-local-.*|print-.*|debugvars|help|test-.*with.*|upload-coverage|vm-images)
NO_DEPS_TARGETS := ${NO_DEPS_TARGETS}( ${NO_DEPS_TARGETS})*
# colors for pretty output. Unless we are in Azure pipelines
ifeq (${PIPELINE_WORKSPACE},)
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
CYAN := \033[36m
NOCOLOR := \033[0m
endif

# All of our .sh script assumes bash. On system such as Ubuntu, the makefile
# needs to explicitly points to bash.
SHELL=/bin/bash

# Helper when we need to make sure IMAGE_VERSION is defined and not 'latest'
.check_image_version:
	@if [[ -z "${IMAGE_VERSION}" || "${IMAGE_VERSION}" == "latest" ]] ; then \
		echo -e "${RED}IMAGE_VERSION should be set to a unique value. e.g. ci-695.${NOCOLOR}" ; \
		false; \
	fi

.check_packer: # check if packer is installed and is the correct one
	@if ! command -v packer; then \
		echo -e "${RED}Packer (https://www.packer.io/) is not found. Please install it first${NOCOLOR}" ; \
		false; \
	fi
	@if [ "$$(basename $$(realpath $$(which packer)))" != "packer" ] ; then \
		echo -e "${RED}Packer is found as '$$(which packer)', but seems to be fake. Please check https://www.packer.io/ is installed and is in the PATH${NOCOLOR}" ; echo \
		false; \
	fi

# common targets. They won't interfere with default targets due to 'default:all' at the top

# 'Help' target - based on '##' comments in targets
#
# This target ("help") scans Makefile for '##' in targets and prints a summary
# Note - used awk to print (instead of echo) so escaping/coloring is platform independed
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n make $(CYAN)<target>$(NOCOLOR)\n" } \
	/^[.a-zA-Z0-9_ -]+[ \t]*:.*?##/ { printf " $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo 'For specific help in folders, try "(cd <dir>; make help)"'
	@echo ""

# Support for simple debug print (make debugvars)
VARS_TO_PRINT ?= \
	DIMG TEST_IMG BUILDENV_PATH TESTENV_PATH BUILDENV_IMG BUILDENV_DOCKERFILE KM_BIN \
	DTYPE TEST_IMG_REG BUILDENV_IMG_REG SRC_BRANCH SRC_SHA \
	TOP FROMTOP BLDTOP BLDDIR SUBDIRS KM_BLDDIR KM_BIN \
	CFLAGS BLDEXEC BLDLIB COPTS USER \
	COVERAGE COVERAGE_REPORT SRC_BRANCH IMAGE_VERSION \
	CLOUD_RESOURCE_GROUP CLOUD_SUBSCRIPTION CLOUD_LOCATION \
	K8_SERVICE_PRINCIPAL REGISTRY_NAME REGISTRY REGISTRY_AUTH_EXAMPLE REGISTRY_SKU


.PHONY: debugvars
debugvars: ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))

# allows to do 'make print-varname'
print-% : ; @echo $*='$($*)'
