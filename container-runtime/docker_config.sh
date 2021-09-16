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

# Install docker configuration to allow krun to be used as a runtime.

# exit if any command fails, don't execute commands if TRACE has a value.
set -e ; [ "$TRACE" ] && set -x

# This script must be run as root.
[ `id -u` != "0" ] && echo "Must run as root" && exit 1

# Can we assume docker is installed?
#apt-get update
#apt-get install -y -q docker.io
#dnf install -y -q moby-engine

# If docker is not here, don't do anything.
DOCKERPATH=$(which docker) || echo "Docker is not present on this system" && exit 0

# docker config file locations
ETC_DAEMON_JSON=/etc/docker/daemon.json
KRUN_PATH=/opt/kontain/bin/krun

RESTART_DOCKER_FEDORA="systemctl restart docker.service "
RESTART_DOCKER_UBUNTU="service docker restart"

. /etc/os-release
[ "$ID" != "fedora" -a "$ID" != "ubuntu" ] && echo "Unsupported linux distribution: $ID" && exit 1

function restart_docker()
{
   local RESTART_DOCKER=RESTART_DOCKER_${ID^^}
   echo "Restarting docker with: ${!RESTART_DOCKER}"
   ${!RESTART_DOCKER}
}

# UNINSTALL
if [ $# -eq 1 -a "$1" = "-u" ]; then
   echo "Removing kontain docker config changes"
   if [ "$DOCKERPATH" != "" ]; then
      if [ -e $ETC_DAEMON_JSON.kontainsave ]; then
         cp $ETC_DAEMON_JSON.kontainsave $ETC_DAEMON_JSON
         rm -f $ETC_DAEMON_JSON.kontainsave
      else
         # No .kontainsave file, they didn't have a daemon.json file
         rm -f $ETC_DAEMON_JSON
      fi
      restart_docker
   fi
   exit 0
fi

# We configure docker to use krun here.  krun may need some packages that
# are not installed by default.  We don't install them here but instead depend
# on podman_config.sh to install them for us.
if [ ! -e $ETC_DAEMON_JSON ]; then
   # doesn't exist, create what we need
   echo "$ETC_DAEMON_JSON does not exist, creating"
   cat <<EOF >/tmp/daemon.json$$
{
  "runtimes": {
    "krun": {
      "path": "$KRUN_PATH"
    }
  }
}
EOF
   # get docker to ingest the new daemon.json file
   mkdir -p `dirname $ETC_DAEMON_JSON`
   cp /tmp/daemon.json$$ $ETC_DAEMON_JSON
   rm -fr /tmp/daemon.json$$
   restart_docker
else
   # update existing file.  I've found if the file is not really json (it is an empty file),
   # jq fails without returning an error.
   cp $ETC_DAEMON_JSON $ETC_DAEMON_JSON.kontainsave
   krun=`jq '.runtimes["krun"]' $ETC_DAEMON_JSON`
   if test "$krun" = "null"
   then
      jq '.runtimes["krun"].path = "/opt/kontain/bin/krun"' $ETC_DAEMON_JSON >/tmp/daemon.json$$
      cp /tmp/daemon.json$$ $ETC_DAEMON_JSON
      # get docker to ingest the new daemon.json file
      if test $? -eq 0
      then
         restart_docker
      fi
      rm -f /tmp/daemon.json$$
   else
      echo "krun already configured in $ETC_DAEMON_JSON"
      echo "\"krun\": $krun"
   fi
fi
