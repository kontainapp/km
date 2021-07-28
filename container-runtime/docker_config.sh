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

DEBUG=echo
#DEBUG=

# docker config file locations
ETC_DAEMON_JSON=/etc/docker/daemon.json

id=`grep ^ID= /etc/os-release`

function restart_docker()
{
   echo "Restarting docker"
   if test $id = "ID=fedora"
   then
      $DEBUG sudo systemctl restart docker.service   # fedora
   elif test $id = "ID=ubuntu"
   then
      $DEBUG sudo service docker restart   # ubuntu
   else
      echo "unknown linux distribution $id"
      false
   fi
}

# Can we assume docker is installed?
#sudo apt-get install -y -q docker.io
#sudo dnf install -y -q moby-engine

if ! sudo test -e $ETC_DAEMON_JSON
then
   # doesn't exist, create what we need
   cat <<EOF >/tmp/daemon.json$$
{
  "runtimes": {
    "krun": {
      "path": "/opt/kontain/bin/krun"
    }
  }
}
EOF
   # get docker to ingest the new daemon.json file
   sudo cp /tmp/daemon.json$$ $ETC_DAEMON_JSON
   rm -fr /tmp/daemon.json$$
   restart_docker
else
   # update existing file.  I've found if the file is not really json (it is an empty file),
   # jq fails without returning an error.
   krun=`sudo jq '.runtimes["krun"]' $ETC_DAEMON_JSON`
   if test "$krun" = "null"
   then
      sudo jq '.runtimes["krun"].path = "/opt/kontain/bin/krun"' $ETC_DAEMON_JSON >/tmp/daemon.json$$
      sudo cp /tmp/daemon.json$$ $ETC_DAEMON_JSON
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
