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

NGINX_REPO := git@github.com:nginx/nginx.git
NGINX_BRANCH := branches/stable-1.16

all: build link

nginx: |
	git clone ${NGINX_REPO} -b ${NGINX_BRANCH}

fromsrc: nginx build link

build:
	./build.sh

link:
	./link-km.sh

clobber: clean
	rm -rf nginx

clean:
	rm -rf nginx.kmd
