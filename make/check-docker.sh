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
# Check for docker availability, report issues and exit(1) if something is wrong
[ "$TRACE" ] && set -x

RED="\033[31m"
NOCOLOR="\033[0m"

ver=`docker version -f '{{.Server.APIVersion}}'`
if [ $? -ne 0 ] ; then
   echo -e "${RED}Docker is not installed or not running${NOCOLOR}"
   exit 1
fi

# We need to check API version, not marketing version (which can change with git forks by cloud providers)
# See https://github.community/t5/GitHub-Actions/What-really-is-docker-3-0-6/td-p/30752 for details
# So after we validated aconnection to docker server, we need to check the client API version
docker version -f '{{.Client.APIVersion}}' | (
   IFS=. read major minor;
   if [[ -z "$major" || $major -lt 1 || "$minor" -lt 12 ]] ; then
         echo -e "${RED}Docker  API version $major.$minor is not supported.  We expect 1.12 or later${NOCOLOR}"
         exit 1
   fi
   echo Docker API version $major.$minor
)