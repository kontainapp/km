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

# this is the path from the TOP to current dir
FROMTOP := $(shell git rev-parse --show-prefix)
# Current branch (for making different names unique per branch, e.g. Docker tags)
# use 'SRC_BRANCH=branch make <target>' for building target (e.g clean :-)) for other branches, if needed
SRC_BRANCH ?= $(shell git rev-parse --abbrev-ref  HEAD)

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

# cloud-related stuff. By default set to Azure
#
# name of the cloud, as well as subdir of $(TOP)/cloud where the proper scripts hide
CLOUD ?= azure
CLOUD_SCRIPTS := $(TOP)cloud/$(CLOUD)

# now bring all cloud-specific stuff needed in forms on 'key = value'
include $(CLOUD_SCRIPTS)/cloud_config.mk

# Use current branch as image version (tag) for doccker images.
# To be complianbt with tag grammar, replace '/' with '-'
IMAGE_VERSION = $(subst /,-,$(SRC_BRANCH))

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
