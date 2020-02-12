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


TOP := $(shell git rev-parse --show-cdup)
REAL_TOP := $(realpath ${TOP})
CURRENT := ${REAL_TOP}/payloads/redis
REDIS_TOP := ${CURRENT}/redis
REDIS_EXECUTABLE := ${REDIS_TOP}/src/redis-server
REDIS_KM := ${CURRENT}/redis-server.kmd

REDIS_REPO := git@github.com:antirez/redis.git
REDIS_BRANCH := 6.0

all: ${REDIS_KM} ## Build and Link

fromsrc: ${REDIS_KM} ## Building starting from cloning the source

${REDIS_EXECUTABLE}: ${REDIS_TOP}
	./build.sh

${REDIS_KM}: ${REDIS_EXECUTABLE}
	./link-km.sh

${REDIS_TOP}: |
	git clone ${REDIS_REPO} -b ${REDIS_BRANCH}

clobber: clean ## Cleans everything
	rm -rf ${REDIS_TOP}

clean: ## Clean the build artifacts. Doesn't clean redis source code.
	rm -f ${REDIS_KM}
	-cd ${REDIS_TOP} && make clean