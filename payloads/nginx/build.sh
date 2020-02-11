#!/bin/bash

TOP=$(git rev-parse --show-cdup)
REAL_TOP=$(realpath ${TOP})
CURRENT=${REAL_TOP}/payloads/nginx
NGINX_TOP=${CURRENT}/nginx

BUILDENV_IMAGE_NAME="kontain/buildenv-nginx-alpine"

if [ ! -d ${NGINX_TOP} ]
then
    echo "${NGINX_TOP} doesn't exist. Need to run make fromsrc"
    exit 1
fi

if [ -z $(docker image ls -q ${BUILDENV_IMAGE_NAME}) ]
then
    docker build -t ${BUILDENV_IMAGE_NAME} -f buildenv.dockerfile ${CURRENT}
fi

BUILDENV_CONTAINER_NAME="kontain-nginx-payload-build"

docker run --rm \
    -it \
    -d \
    -v ${NGINX_TOP}:/nginx:Z \
    -w /nginx \
    --name ${BUILDENV_CONTAINER_NAME} \
    -u $(id -u ${USER}):$(id -g ${USER}) \
    ${BUILDENV_IMAGE_NAME}

docker exec -w /nginx ${BUILDENV_CONTAINER_NAME} \
    ./auto/configure \
    --prefix=/opt/nginx

docker exec -w /nginx ${BUILDENV_CONTAINER_NAME} \
    make

docker stop ${BUILDENV_CONTAINER_NAME}
