#!/bin/bash

TOP=$(git rev-parse --show-cdup)
REAL_TOP=$(realpath ${TOP})
CURRENT=${REAL_TOP}/payloads/redis
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
    ${BUILDENV_IMAGE_NAME} \
    make
