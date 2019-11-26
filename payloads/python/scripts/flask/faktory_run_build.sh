#!/bin/bash
#
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
# A wrapper to form proper Dockerfile for Faktory and run docker build.
# Docker build will analyze the content of original image (src-image), generate needed files
#     and create a related Kontainer (km-image).
# dtype below is distribution type, needed to pick correct example (we use it as image tag). TODO: drop it
#
# USAGE:
#  ./faktory_run_build.sh src-image target-km-image dtype
#
#
# TODO:
#  - think about language-depended hooks
#        - e.g. for python we need to check for '-E' flag and strip it (to allow PYTHONPATH setting)
#  - get list of modules to link in and add python.km build (if needed)
#
IMAGE="$1"
KM_IMAGE="$2"
DTYPE="$3"
KM_BIN="$4"

if [ -z "${IMAGE}" -o -z "${KM_IMAGE}" -o -z "${DTYPE}" ] ; then echo Wrong usage, missing param; exit 1; fi


RED="\033[31m"
NOCOLOR="\033[0m"

docker_build="docker build --no-cache"
entrypoint=$(docker image inspect $IMAGE -f '{{json .Config.Entrypoint }}')

# Note: for python, -E flag is a killer - blocks PYTHONHOME setting, so warn.
# TODO - here is a good place to check language-specific quirks with entrypoint and cmd and env, i.e usage of scripts?
# or (better) pass this info down as build_param so in-container faktory_prepare can analyze..
if [[ $entrypoint == *python*-*E* ]] ; then
   echo -e "${RED}*** WARNING: -E flag in python - PYTHONHOME wont work${NOCOLOR}"  >&2
fi

cp ${KM_BIN} .
cp ../../cpython/python.km python3.km # TODO - build python.km if needed; also use python versioning scheme

cat faktory_stem.dockerfile  - <<EOF  | $docker_build --build-arg distro="$DTYPE"  -t $KM_IMAGE -f - .

#-- auto-lifted from $IMAGE. Do not edit directly:--

WORKDIR    $(docker image inspect $IMAGE -f '{{json .Config.WorkingDir }}')
ENTRYPOINT $entrypoint
CMD        $(docker image inspect $IMAGE -f '{{json .Config.Cmd }}')

EOF

res=$?
rm -f python3.km $(basename ${KM_BIN})
exit $res