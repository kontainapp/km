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
# Makefile for building and packing Redis payload runtime for KM


REDIS_REPO := git@github.com:antirez/redis.git
REDIS_BRANCH := 6.0

all: build link ## Build and Link

redis: |
	git clone ${REDIS_REPO} -b ${REDIS_BRANCH}

fromsrc: redis build link ## Building starting from cloning the source

build:
	./build.sh

link:
	./link-km.sh

clobber: clean ## Cleans everything
	rm -rf redis

clean: ## Clean the build artifacts. Doesn't clean redis source code.
	rm -f ${CURRENT}/redis-server.kmd