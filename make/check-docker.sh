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

RED="\033[31m"
NOCOLOR="\033[0m"

ver=`docker version -f '{{.Server.Version}}'`
if [ $? -ne 0 ] ; then
   echo -e "${RED}Docker is not installed or not running${NOCOLOR}"
   exit 1
fi

# Server.Version may be 'dev' (in Moby) or 'NN.xx' in docker distros.
# But the client version is always NN.xx - so now that we know server is up, we check the client version
ver=`docker version -f '{{.Client.Version}}'`
major=`echo $ver| sed 's/\..*$//'`
echo Docker version $ver major: $major
if [ -z "$ver" -o $major -lt 18 ] ; then
      echo -e "${RED}Docker version '$ver' is not supported.  We expect 18 or later${NOCOLOR}"
      exit 1
fi;