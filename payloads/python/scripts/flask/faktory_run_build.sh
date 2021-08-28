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
   echo -e "${RED}*** WARNING: -E flag in python - PYTHONHOME wont work${NOCOLOR}" >&2
fi

cat faktory_stem.dockerfile - <<EOF  | $docker_build --build-arg distro="$DTYPE" -t $KM_IMAGE -f - .
#-- auto-lifted from $IMAGE. Do not edit directly:--

WORKDIR    $(docker image inspect $IMAGE -f '{{json .Config.WorkingDir }}')
ENTRYPOINT $entrypoint
CMD        $(docker image inspect $IMAGE -f '{{json .Config.Cmd }}')

EOF
