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
# Makefile for building and packing Nginx payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)

COMPONENT := nginx
NGINX_REPO := git@github.com:nginx/nginx.git
NGINX_BRANCH ?= release-1.20.0

# building it in alpine container
DTYPE := alpine

CURRENT := ${TOP}/payloads/nginx
NGINX_TOP = ${CURRENT}/nginx
NGINX_OBJ_LOC = ${NGINX_TOP}/objs
NGINX_EXECUTABLE = ${NGINX_OBJ_LOC}/nginx
NGINX_KM = ${BLDDIR}/nginx.km

include ${TOP}/make/images.mk

all: ${NGINX_KM}

fromsrc: ${NGINX_KM}  ## Clone the repo if needed, and build nginx (default)

${NGINX_KM}: ${NGINX_EXECUTABLE} .check link-km.sh
	@mkdir -p ${BLDDIR}
	./link-km.sh ${NGINX_OBJ_LOC} ${KM_OPT_BIN} ${BLDDIR}

${NGINX_EXECUTABLE}: ${NGINX_TOP}
	./build.sh

${NGINX_TOP}: |
	git clone ${NGINX_REPO} -b ${NGINX_BRANCH}

clobber: ## remove all sources and build artifacs
	rm -rf ${NGINX_TOP} ${NGINX_KM}

clean: ## keep sources but clean up build artifacts
	rm -rf ${NGINX_KM} ${BLDDIR}
	-cd ${NGINX_TOP} && make clean

install: ${NGINX_KM} ## install to /opt/kontain/nginx (as root)
	sudo make -C nginx install

try: ${NGINX_KM} ## try to run nginx (*as root)
	sudo ${KM_BIN} -Vhyper ${NGINX_KM}

DEP_PACKAGES := pcre-static
.PHONY: .check
.check: # a helper to check for needed packages
	@for i in ${DEP_PACKAGES} ; do \
		if ! rpm -qa | grep $$i -q ; then \
			failed=1; echo -e "Missing $$i. To install, run \nsudo dnf install $$i" ; \
		fi ; \
		if [ -n "$$failed" ] ; then false ; fi \
	done