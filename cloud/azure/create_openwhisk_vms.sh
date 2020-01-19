#!/bin/bash
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
# Deploys VMs for Openwhisk demo to Azure.
# Also preps this VM to fetch from Kontain github
set -e
[ "$TRACE" ] && set -x

source `dirname $0`/cloud_config.mk

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
