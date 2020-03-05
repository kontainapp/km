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
# Makefile for building and packing Nginx payload runtime for KM

TOP := $(shell git rev-parse --show-toplevel)
CURRENT := ${TOP}/payloads/nginx
NGINX_TOP := ${CURRENT}/nginx
NGINX_EXECUTABLE := ${NGINX_TOP}/objs/nginx
NGINX_KM := ${CURRENT}/nginx.kmd

NGINX_REPO := git@github.com:nginx/nginx.git
NGINX_BRANCH := branches/stable-1.16

all: ${NGINX_KM}

fromsrc: ${NGINX_KM}

${NGINX_KM}: ${NGINX_EXECUTABLE}
	./link-km.sh

${NGINX_EXECUTABLE}: ${NGINX_TOP}
	./build.sh

${NGINX_TOP}: |
	git clone ${NGINX_REPO} -b ${NGINX_BRANCH}

clobber: clean
	rm -rf ${NGINX_TOP}

clean:
	rm -rf ${NGINX_KM}
	-cd ${NGINX_TOP} && make clean
