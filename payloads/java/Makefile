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
# Makefile for building and packing Java payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

JDK_VERSION := jdk-11.0.8+10
# COMPONENT is used in docker image names so no '+' allowed, and we don't care about minor opt
COMPONENT := jdk-11.0.8
PAYLOAD_NAME := ${COMPONENT}

JDK_BUILD_DIR = ${JDK_VERSION}/build/linux-x86_64-normal-server-release

define testenv_preprocess
	./link-km.sh ${JDK_BUILD_DIR} /home/appuser/km/build/opt/kontain
endef

TESTENV_EXTRA_FILES := ${JDK_BUILD_DIR}/images/jdk/* ${JDK_BUILD_DIR}/images/jdk/bin/java.kmd ${APP_JAR} \
	${TOP}/tests/bats ${TOP}/tests/bats-assert ${TOP}/tests/bats-support \
	${TOP}/tests/run_bats_tests.sh \
	${TOP}/build/payloads

# this is how we are running tests in the container
CONTAINER_TEST_CMD := ./scripts/test-run.sh

# Needed to build Java from source - the same list as in buildenv-fedora.dockerfile
DEP_PACKAGES := java-11-openjdk \
	java-11-openjdk-devel \
	autoconf \
	zip \
	unzip \
	fontconfig-devel \
	cups-devel \
	alsa-lib-devel \
   	libXtst-devel \
	libXt-devel \
	libXrender-devel \
	libXrandr-devel \
	libXi-devel

# 'yes' if java was build from src (based on existable of .git repo)
FROMSRC=$(shell test -d $(JDK_VERSION)/.git && echo yes)

# config for image validation
RUNENV_VALIDATE_CMD := ("java" "-cp" "/scripts" Hello)
RUNENV_VALIDATE_EXPECTED := Hello, World!
RUNENV_VALIDATE_DEPENDENCIES := scripts/Hello.class \
	scripts/SimpleHttpServer.class \
	app.kontain/app/kontain/snapshots/Snapshot.class \

# runenv-image configs
export define runenv_prep
	./link-km.sh ${JDK_BUILD_DIR} /opt/kontain
	mkdir -p ${RUNENV_PATH} && cp --recursive --preserve=all ${JDK_BUILD_DIR}/images/jdk/* ${RUNENV_PATH}
	mv ${RUNENV_PATH}/bin/java.kmd ${RUNENV_PATH}/bin/java
endef

include ${TOP}/make/images.mk

TEST_DEPENDENCIES := \
	scripts/Hello.class \
	scripts/SimpleHttpServer.class \
	${BLDDIR}/lib/libkm_java_native.so \
	# ${BLDDIR}/html/index.html \
	# sb.jar \
	# test_java.bats 


all: ${TEST_DEPENDENCIES} 

${TEST_DEPENDENCIES}: ${JDK_BUILD_DIR}/images/jdk/bin/java.kmd

${JDK_BUILD_DIR}/images/jdk/bin/java.kmd: 
ifneq ($(FROMSRC),yes)
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:${TOP}:z \
		${KM_OPT_VOLUME} \
		-w ${TOP} \
		${BUILDENV_IMG_TAGGED} \
		make -C payloads/java in-blank-container
else
	./link-km.sh ${JDK_BUILD_DIR}
endif

JAVA_DIR := ${JDK_BUILD_DIR}/images/jdk

fromsrc: .check ${JDK_VERSION} jtreg ${KM_BIN} ${KM_LDSO}
	@if [ "${FROMSRC}" != "yes" ] ; then \
		echo -e "${RED}${JDK_VERSION} is not a git repo. Did you do 'make clobber' first ? ${NOCOLOR}" ; \
		fail; \
	fi
	@# -fcommon gcc flag is needed for gcc10, see https://bugs.openjdk.java.net/browse/JDK-8235903
	cd ${JDK_VERSION} && bash configure \
		--disable-warnings-as-errors --with-native-debug-symbols=none \
		--with-extra-cflags='-fcommon'  \
		--with-jvm-variants=server --with-zlib=bundled --with-jtreg=$(realpath jtreg) \
		--enable-jtreg-failure-handler
	make -C ${JDK_VERSION} images
	./link-km.sh ${JDK_BUILD_DIR}
	# build HotSpot disassember plugin. Useful for failure analysis.
	curl --output ${BLDDIR}/binutils-2.19.1.tar.bz2 https://ftp.gnu.org/gnu/binutils/binutils-2.19.1.tar.bz2
	bunzip2 ${BLDDIR}/binutils-2.19.1.tar.bz2
	(cd ${BLDDIR} ; tar --overwrite -xf binutils-2.19.1.tar)
	make -C ${JDK_VERSION}/src/utils/hsdis BINUTILS=${BLDDIR}/binutils-2.19.1 CFLAGS="-Wno-error -fPIC" all64
	cp ${JDK_VERSION}/src/utils/hsdis/build/linux-amd64/hsdis-amd64.so ${JAVA_DIR}/lib/hsdis-amd64.so

${JDK_VERSION}:
	git clone https://github.com/openjdk/jdk11u.git ${JDK_VERSION} -b ${JDK_VERSION}

JAVA_DIR := ${JDK_BUILD_DIR}/images/jdk

# Build the API into a JPMS compatable JMOD file.
# Output:
#  ${BLDDIR}/mods  - Directory: KM API Java Pieces
#  ${BLDDIR}/lib   - Directory: KM API native library(ies)
#  ${BLDDIR}/html  - Directory: KM API documentation (javadoc)
#  ${BLDDIR}/app.kontain.jmod - JPMS JMod file. Useful for customers to build
#                               custom runtimes with JLINK

api: ${BLDDIR}/app.kontain.jmod ${BLDDIR}/html/index.html

${BLDDIR}/app.kontain.jmod: ${BLDDIR}/mods/app.kontain ${BLDDIR}/lib/libkm_java_native.so
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:${TOP}:z \
		-w ${CURDIR} \
		${BUILDENV_IMG_TAGGED} \
		${JDK_BUILD_DIR}/images/jdk/bin/jmod create \
		--class-path ${BLDDIR}/mods/app.kontain/ \
		--libs ${BLDDIR}/lib $@

${BLDDIR}/mods/app.kontain: app.kontain/module-info.java
	mkdir -p ${BLDDIR}/mods
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:${TOP}:z \
		-w ${CURDIR} \
		${BUILDENV_IMG_TAGGED} \
		${JDK_BUILD_DIR}/images/jdk/bin/javac -d ${BLDDIR}/mods --module-source-path . --module app.kontain $?

${BLDDIR}/lib/libkm_java_native.so: app.kontain/app/kontain/snapshots/km_java_native_snapshot.c
	mkdir -p ${BLDDIR}/lib
	gcc -fPIC -shared -O3 -I/usr/include -I${JAVA_DIR}/include -I${JAVA_DIR}/include/linux \
		$? -o $@

${BLDDIR}/html/index.html: app.kontain/app/kontain/snapshots/Snapshot.java
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:${TOP}:z \
		-w ${CURDIR} \
		${BUILDENV_IMG_TAGGED} \
		${JDK_BUILD_DIR}/images/jdk/bin/javadoc -d ${BLDDIR}/html $?

scripts/%.class: scripts/%.java
	${DOCKER_RUN_BUILD} \
		-v ${TOP}:${TOP}:z \
		-w ${CURDIR} \
		${BUILDENV_IMG_TAGGED} \
		${JAVA_DIR}/bin/javac $<

in-blank-container: ## invoked in blank container by ``make all''. DO NOT invoke manually
	mkdir -p ${JDK_BUILD_DIR}
	tar -C /home/appuser/java -cf - . | tar -C ${JDK_BUILD_DIR} -xf -
	./link-km.sh ${JDK_BUILD_DIR}

REPO_COMMIT := f7f2acb31d27f57550d6a9d37bbe50e185d1ca4b
SRC_REPO_DIR := rest-service-11
SRC_DIR := ${SRC_REPO_DIR}/complete
APP_JAR := ${SRC_DIR}/build/libs/rest-service-0.0.1-SNAPSHOT.jar

${SRC_REPO_DIR}:
	git clone https://github.com/spring-guides/gs-rest-service.git ${SRC_REPO_DIR}
	cd ${SRC_REPO_DIR}; git reset --hard ${REPO_COMMIT}

${APP_JAR}: | ${SRC_REPO_DIR}
	docker run --rm -v $$(pwd)/${SRC_REPO_DIR}:/app:rw -w /app/complete -u $(shell id -u):$(shell id -g) openjdk:8u342-jdk ./gradlew -q --console=plain build

sb.jar: ${APP_JAR}
	cp ${APP_JAR} sb.jar

JAVA_LD_PATH := ${CURDIR}/${JAVA_DIR}/lib/server:${CURDIR}/${JAVA_DIR}/lib/jli:${CURDIR}/${JAVA_DIR}/lib
test: ${TEST_DEPENDENCIES} ## Basic test run
	JAVA_DIR=${CURDIR}/${JAVA_DIR} JAVA_LD_PATH=${JAVA_LD_PATH} BLDDIR=${BLDDIR} \
		${TOP}/tests/run_bats_tests.sh --km=${KM_BIN} \
		--tests=${TOP}/payloads/java/test_java.bats --test-type=dynamic

test-all: test

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
	rm -rf ${BLDDIR}/*

clean:
	rm -f ${JDK_BUILD_DIR}/images/jdk/bin/java.k*
	rm -f app/kontain/*.class app/kontain/*.so
	rm -f app/kontain/snapshots/*.class app/kontain/snapshots/*.so
	rm -rf ${SRC_REPO_DIR} sb.jar
	find . -name "*.class" -type f -delete
	rm -rf ${BLDDIR}/*

# This defines aliases to be used when publishing to github
RUNENV_TAG := java-11 jdk-11 ${COMPONENT}

