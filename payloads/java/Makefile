# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Makefile for building and packing Java payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)
CURRENT_DIR := ${TOP}/payloads/java
JDK_VERSION := jdk-11.0.6+10
# COMPONENT is used in docker image names so no '+' allowed, and we don't care about minor opt
COMPONENT := jdk-11.0.6
PAYLOAD_NAME := ${COMPONENT}

JDK_BUILD_DIR = ${JDK_VERSION}/build/linux-x86_64-normal-server-release

# Needed to build Java from source - the same list as in buildenv-fedora.dockerfile
DEP_PACKAGES := java-11-openjdk \
	java-11-openjdk-devel  \
	autoconf \
	zip \
	unzip \
	fontconfig-devel \
	cups-devel  \
	alsa-lib-devel \
   	libXtst-devel \
	libXt-devel \
	libXrender-devel \
	libXrandr-devel \
	libXi-devel

# 'yes' if java was build from src (based on existable of .git repo)
FROMSRC=$(shell test -d $(JDK_VERSION)/.git && echo yes)

# docker build dir for runenv-image
RUNENV_PATH := .
# config for image validation
RUNENV_VALIDATE_DIR := scripts
RUNENV_VALIDATE_CMD := -cp /scripts Hello
RUNENV_VALIDATE_EXPECTED := Hello, World!
RUNENV_VALIDATE_DEPENDENCIES := scripts/Hello.class
RUNENV_DEMO_DEPENDENCIES := scripts/SimpleHttpServer.class

all: ## Link java.kmd based on either full build or build artifats in docker image
ifneq ($(FROMSRC),yes)
	${DOCKER_RUN} -v ${TOP}:${TOP}:Z -w ${TOP} ${BUILDENV_IMG}:${BUILDENV_IMAGE_VERSION} \
		make -C payloads/java in-blank-container
else
	./link_km.sh ${JDK_BUILD_DIR}
endif

fromsrc: .check ${JDK_VERSION} jtreg ${KM_BIN} ${KM_LDSO}
	@if [ "${FROMSRC}" != "yes" ] ; then \
		echo -e "${RED}${JDK_VERSION} is not a git repo. Did you do 'make clobber' first ? ${NOCOLOR}" ; \
		fail; \
	fi
	cd ${JDK_VERSION} && bash configure \
		--disable-warnings-as-errors --with-native-debug-symbols=internal \
		--with-jvm-variants=server --with-zlib=bundled --with-jtreg=$(realpath jtreg) \
		--enable-jtreg-failure-handler
	make -C ${JDK_VERSION} images
	./link_km.sh ${JDK_BUILD_DIR}

${JDK_VERSION}:
	git clone https://github.com/openjdk/jdk11u.git ${JDK_VERSION} -b ${JDK_VERSION}

scripts/%.class: scripts/%.java
	${DOCKER_RUN} \
		-v ${TOP}:${TOP}:Z \
		-w ${CURRENT_DIR} \
		${BUILDENV_IMG}:${BUILDENV_IMAGE_VERSION} \
		${JDK_BUILD_DIR}/images/jdk/bin/javac $?

in-blank-container: ## invoked in blank container by ``make all''. DO NOT invoke manually
	mkdir -p ${JDK_BUILD_DIR}
	tar -C /home/appuser/java -cf - . | tar -C ${JDK_BUILD_DIR} -xf -
	./link_km.sh ${JDK_BUILD_DIR}

.PHONY: .check
.check: # a helper to check for needed packages
	@for i in ${DEP_PACKAGES} ; do \
		if ! rpm -qa | grep $$i -q ; then \
			failed=1; echo -e "Missing $$i. To install, run \nsudo dnf install $$i" ; \
		fi ; \
		if [ -n "$$failed" ] ; then false ; fi \
	done

jtreg:
	tar xf jtreg-4.2-b16.tar.gz

clobber:
	rm -rf jtreg ${JDK_VERSION}

clean:
	rm -f ${JDK_BUILD_DIR}/images/jdk/bin/java.k*
	find . -name "*.class" -type f -delete

include ${TOP}/make/images.mk

