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
# Makefile for packing dynamic Python payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

# Git tag to fetch.
# TAG ?= v3.7.4
TAG ?= v3.8.6

# Version (used in python paths) and ABI flags (used in *some* paths)
VERS = $(shell echo ${TAG} | sed -e 's/^v//' -e 's/.[0-9]*$$//')
# in Python 3.8+ ABI flag 'm' is no longer used, see https://bugs.python.org/issue36707.
ABI = $(shell if [ $$(echo ${VERS} | sed 's/^[0-9]*.//') -le 7 ] ; then echo m; fi)

BUILDENV_IMAGE_EXTRA_ARGS = --label "VERS=${VERS}" --build-arg=VERS=${VERS} --build-arg=ABI=${ABI} --build-arg=TAG=${TAG}
TESTENV_IMAGE_EXTRA_ARGS  = --label "VERS=${VERS}" --build-arg=VERS=${VERS} --build-arg=ABI=${ABI}
RUNENV_IMAGE_EXTRA_ARGS   = --label "VERS=${VERS}" --build-arg=VERS=${VERS}
TESTENV_EXTRA_FILES := $(wildcard /opt/kontain/lib/libmimalloc.so*)

# Location of cpython sources. When building in container, it is set in buildenv dockerfile.
PYTHONTOP ?= ${TOP}/payloads/python/cpython

# this is how we run python.km tests in container
CONTAINER_TEST_CMD := ./scripts/test-run.sh ./python

# Component id for docker images.
# Note that for now it is version-independent (i.e. not python${VERS})
# to avoid version dependencies downstream in FROM/demo images etc.
COMPONENT := dynamic-python

# KM file name, relative to currentdir. Note that the same will be absolute in the container
PAYLOAD_KM := cpython/python.kmd.mimalloc

# name to be added to label(s)
PAYLOAD_NAME := ${COMPONENT}

# List of artifacts necessary to run tests. See buildenv-fedora.dockerfile
PYTHON_DISTRO_FILES := $(addprefix cpython/, Lib Modules build pybuilddir.txt dlstatic_km.mk python.km.symbols.c)

# Set info for runenv-image build

RUNENV_ROOTFS_SCRIPT := ${TOP}/payloads/python/runenv/rootfs.sh
RUNENV_DEMO_PATH := ${TOP}/payloads/python
RUNENV_VALIDATE_CMD := ("/usr/local/bin/python3" "scripts/sanity_test.py")

export define runenv_prep
	${RUNENV_ROOTFS_SCRIPT} ${PYTHONTOP} ${RUNENV_PATH} ${VERS} $(notdir ${PAYLOAD_KM}) dynamic
endef

RELEASE_TAG := python python-${VERS}

include ${TOP}/make/images.mk
