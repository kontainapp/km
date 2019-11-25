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
# A helper include. ALlowes to include some vars in dirs which cannot include
# actions.mk (e.g. tests which need their own compike /link flags)

default: all

# this is the path from the TOP to current dir
FROMTOP := $(shell git rev-parse --show-prefix)
# Current branch and SHA(for making different names unique per branch, e.g. Docker tags)
SRC_BRANCH ?= $(shell git rev-parse --abbrev-ref  HEAD)
SRC_SHA ?= $(shell git rev-parse HEAD)

PATH := $(realpath ${TOP}tools):${PATH}

# sha and build time for further reporting
SRC_VERSION := $(shell git rev-parse HEAD)
BUILD_TIME := $(shell date -Iminutes)

# all build results (including obj etc..)  go under this one
BLDTOP := ${TOP}build/
# Build results go here.
# For different build types (e.g. coverage), pass BLDTYPE=<type>/, e.g BLDTYPE=coverage/ (with trailing /)
BLDDIR := ${BLDTOP}${FROMTOP}$(BLDTYPE)

# km location needs to be fixed no matter what is the FROMTOP,
# so we can use KM from different places
KM_BLDDIR := ${BLDTOP}km/$(BLDTYPE)
KM_BIN := ${KM_BLDDIR}km

# dockerized build
# TODO: Some of these values should be moved to images.mk , but we have multiple
# dependencies on that , so keeping it here for now
#
# use DTYPE=fedora or DTYPE=ubuntu, etc... to get different flavors
DTYPE ?= fedora
USER  ?= appuser

# needed in 'make withdocker' so duplicating it here, for now
BUILDENV_IMG  ?= kontain/buildenv-${COMPONENT}-${DTYPE}

UID := $(shell id -u)
GID := $(shell id -g)
USER_NAME := $(shell id -un)

DOCKER_BUILD := docker build --label "KONTAIN:BRANCH=$(SRC_BRANCH)" --label "KONTAIN:SHA=$(SRC_SHA)"
# Use DOCKER_RUN_CLEANUP="" if container is needed after a run
DOCKER_RUN_CLEANUP ?= --rm
DOCKER_RUN := docker run ${DOCKER_RUN_CLEANUP} -t --ulimit nofile=`ulimit -n`:`ulimit -n -H` -u${UID}:${GID}
DOCKER_RUN_TEST := ${DOCKER_RUN} --device=/dev/kvm

# cloud-related stuff. By default set to Azure
#
# name of the cloud, as well as subdir of $(TOP)/cloud where the proper scripts hide. Use CLOUD='' to build with no cloud
# All cloud stuff can be turned off by passing CLOUD=''
CLOUD ?= azure

ifneq ($(CLOUD),)
# now bring all cloud-specific stuff needed in forms on 'key = value'
CLOUD_SCRIPTS := $(TOP)cloud/$(CLOUD)
include $(CLOUD_SCRIPTS)/cloud_config.mk
endif

# Use current branch as image version (tag) for doccker images.
IMAGE_VERSION ?= latest

# Code coverage support. If enabled, build with code coverage in dedicate dir
COV_BLDTYPE := coverage

# location for html coverage report
COVERAGE_REPORT  = $(KM_BLDDIR)/coverage.html

# Generic support - applies for all flavors (SUBDIR, EXEC, LIB, whatever)

# Support for "make help"
#
# colors for nice color in output
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
CYAN := \033[36m
NOCOLOR := \033[0m
