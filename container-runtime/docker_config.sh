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

# read arguments id any
UNINSTALL=
RUNTIME_NAME="krun"
KRUN_PATH="/opt/kontain/bin/krun"
KM_PATH="/opt/kontain/bin/km"


for arg in "$@"
do
   case "$arg" in
        -u)
            UNINSTALL=yes
        ;;
        --runtime-name=*)
            RUNTIME_NAME="${1#*=}"
        ;;
        --runtime-path=*)
            KRUN_PATH="${1#*=}"
            KM_PATH=$(echo "${KRUN_PATH}" | sed 's/krun/km/')
        ;;
    esac
    shift
done

# check that KRUN_PATH points to an executable file
if [ ! -x "$KRUN_PATH" ] && [ -z "$UNINSTALL" ]; then
   echo "Runtime path must be full path to an existing krun executable"
   exit 1
fi
# check that KM_PATH points to an executable file
if [ ! -x "$KM_PATH" ] && [ -z "$UNINSTALL" ]; then
   echo "KM execuable is not found or is not executable"
   exit 1
fi

# Can we assume docker is installed?
#apt-get update
#apt-get install -y -q docker.io
#dnf install -y -q moby-engine

# If docker is not here, don't do anything.
DOCKERPATH=$(which docker) || echo "Docker is not present on this system"
[ -z $DOCKERPATH ] && exit 0

# docker config file locations
ETC_DAEMON_JSON=/etc/docker/daemon.json

RESTART_DOCKER_FEDORA="systemctl restart docker.service "
RESTART_DOCKER_UBUNTU="service docker restart"
RESTART_DOCKER_AMZN="systemctl restart docker.service "

. /etc/os-release
[ "$ID" != "fedora" -a "$ID" != "ubuntu" -a "$ID" != "amzn" ] && echo "Unsupported linux distribution: $ID" && exit 1

function restart_docker()
{
    local RESTART_DOCKER=RESTART_DOCKER_${ID^^}
    echo "Restarting docker with: ${!RESTART_DOCKER}"
    ${!RESTART_DOCKER}
}

# UNINSTALL
if [ -n "$UNINSTALL" ]; then
    echo "Removing kontain docker config changes"
    if [ "$DOCKERPATH" != "" ]; then
        # remove specified config
        jq --arg RNAME "$RUNTIME_NAME" 'del(.runtimes[$RNAME])' $ETC_DAEMON_JSON > /tmp/daemon.json$$
        cp /tmp/daemon.json$$ $ETC_DAEMON_JSON
        restart_docker
    fi
    exit 0
fi

# We configure docker to use krun here.  krun may need some packages that
# are not installed by default.  We don't install them here but instead depend
# on podman_config.sh to install them for us.
if [ ! -e "$ETC_DAEMON_JSON" ]; then
    # doesn't exist, create what we need
    echo "$ETC_DAEMON_JSON does not exist, creating"
   cat <<EOF >/tmp/daemon.json$$
{
  "runtimes": {
    "$RUNTIME_NAME": {
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
    krun=`jq --arg RNAME "$RUNTIME_NAME" '.runtimes[$RNAME]' $ETC_DAEMON_JSON`
    if test "$krun" = "null"
    then
        jq --arg RNAME "$RUNTIME_NAME" --arg RPATH "$KRUN_PATH" '.runtimes[$RNAME].path=$RPATH'  $ETC_DAEMON_JSON >/tmp/daemon.json$$
        cp /tmp/daemon.json$$ $ETC_DAEMON_JSON
        # get docker to ingest the new daemon.json file
        if test $? -eq 0
        then
            restart_docker
        fi
        rm -f /tmp/daemon.json$$
    else
        echo "$RUNTIME_NAME already configured in $ETC_DAEMON_JSON"
        echo "\"$RUNTIME_NAME\": $krun"
    fi
fi
