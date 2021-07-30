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
# Makefile for building and packing Redis payload runtime for KM


TOP := $(shell git rev-parse --show-toplevel)
CURRENT := ${TOP}/payloads/redis
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

test: ${REDIS_KM}
	./test.sh ${REAL_TOP}/build/km/km ${REDIS_KM}