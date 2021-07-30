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
# Deploys VMs for Openwhisk demo to Azure.
# Also preps this VM to fetch from Kontain github

source `dirname $0`/cloud_config.mk
set -e ; [ "$TRACE" ] && set -x

VM_IMAGE=tunnelbiz:fedora:fedora30:0.0.4
ADMIN=kontain
# names going to be formed as 'name-suffix'
SUFFIX='openwhisk-kontain'
VMS="km docker client"
if [[ ! -z "$DEBUG" ]] ; then DEBUG=echo; fi

for name in $VMS; do
   full_name=$name-$SUFFIX
   ip=$($DEBUG az vm create --resource-group ${CLOUD_RESOURCE_GROUP} \
      --name $full_name --public-ip-address-dns-name $full_name \
      --image ${VM_IMAGE} --size ${K8S_NODE_INSTANCE_SIZE} --admin-username ${ADMIN} \
      --output tsv --query publicIpAddress)
   $DEBUG scp -oStrictHostKeyChecking=no ~/.ssh/id_rsa ssh/* $ADMIN@$ip:.ssh  # ** see 'rm -f' after git clone
   if [[ $name =~ client ]]
   then
      $DEBUG ssh $ADMIN@$ip 'cat .ssh/*.pub >> .ssh/authorized_keys'
      echo 'Skipping openwhisk install for client. TODO - install here whatever is needed on the client'
      echo TODO: git clone what is needed and rm .ssh/id_rsa
   else
      $DEBUG ssh $ADMIN@$ip 'cat .ssh/*.pub >> .ssh/authorized_keys; \
                              sudo dnf install -y git ansible ;\
                              git clone git@github.com:kontainapp/openwhisk-performance.git ;  \
                              cd openwhisk-performance; ansible-playbook playbook.yaml ; rm -f ~/.ssh/id_rsa'
   fi
   # open ports , other that ssh (ssh is opened by default)
   $DEBUG az vm open-port --port 80 --resource-group ${CLOUD_RESOURCE_GROUP} --name $full_name --output ${OUT_TYPE}
done
