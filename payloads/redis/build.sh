#!/bin/bash
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
set -e ; [ "$TRACE" ] && set -x

TOP=$(git rev-parse --show-toplevel)
CURRENT=${TOP}/payloads/redis
REDIS_TOP=${CURRENT}/redis

BUILDENV_IMAGE_NAME="kontain/buildenv-redis-alpine"

if [ ! -d ${REDIS_TOP} ]
then
    echo "${REDIS_TOP} doesn't exist. Need to run make fromsrc"
    exit 1
fi

if [ -z $(docker image ls -q ${BUILDENV_IMAGE_NAME}) ]
then
    docker build -t ${BUILDENV_IMAGE_NAME} -f buildenv.dockerfile ${CURRENT}
fi

docker run --rm \
    -v ${REDIS_TOP}:/redis:Z \
    -w /redis \
    -u $(id -u ${USER}):$(id -g ${USER}) \
    ${BUILDENV_IMAGE_NAME} \
    make
