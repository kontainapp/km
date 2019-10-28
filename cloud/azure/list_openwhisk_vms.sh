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
# Deployes VM with Openwhisk instance to Azure.
# Also preps this VM to fetch from Kontain github

source `dirname $0`/cloud_config.mk
set -e

list=`az vm list -g ${CLOUD_RESOURCE_GROUP} --query "[?contains(name, 'openwhisk-kontain')].id" -o tsv`
if [[ ! -z "$list" ]] ; then
   for i in $list; do
     az vm show -d --id $i |  \
         jq -r '[.name,.hardwareProfile.vmSize,.publicIps,("DNS " + .name + ".westus.cloudapp.azure.com")] | @tsv'
   done
fi
