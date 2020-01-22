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