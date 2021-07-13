#!/bin/bash
#
#  Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
set -e
[ "$TRACE" ] && set -x

TOP=$(git rev-parse --show-toplevel)
NGINX_TOP=${TOP}/payloads/nginx/nginx

BUILDENV_IMAGE_NAME="kontain/buildenv-nginx-alpine"

if [ ! -d ${NGINX_TOP} ]; then
   echo "${NGINX_TOP} doesn't exist. Need to run make fromsrc"
   exit 1
fi

if [ -z $(docker image ls -q ${BUILDENV_IMAGE_NAME}) ]; then
   make buildenv-image
fi

# Clean up any running build container. This will only happen if the previous
# build process is interrupted.
BUILDENV_CONTAINER_NAME="kontain-nginx-payload-build"
if [ "$(docker ps -a -q -f name=${BUILDENV_CONTAINER_NAME})" ]; then
   docker rm -f ${BUILDENV_CONTAINER_NAME}
fi

docker run -it --name ${BUILDENV_CONTAINER_NAME} \
   -v ${NGINX_TOP}:/nginx:Z -w /nginx \
   -u $(id -u ${USER}):$(id -g ${USER}) \
   ${BUILDENV_IMAGE_NAME} \
   sh -c "./auto/configure --prefix=/opt/kontain/nginx &&  make -j $(nproc)"
