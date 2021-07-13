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
# Operates on existing Vm instance with openwhisk-kontain suffix in the name.
# 1st arg is operation. See Usage below for more info
#
source `dirname $0`/cloud_config.mk
SUFFIX='openwhisk-kontain'

[ "$TRACE" ] && set -x
op=${1:-help}
if [[ "$op" == "help" || "$op" == "--help" ]] ; then
   cat <<EOF

Usage: ${0} [operation [suffix]]

  Operation: can be help (default), ls, stop, deallocate, start or delete.
             Note that 'stop' will poweroff the VM but will keep billable resources,
             while 'deallocate' will release resources on expense of slower 'start'
  Suffix: Only VMS with naming containing suffix are impacted. Suffix default is $SUFFIX

EOF
   exit 0
fi

list=$(az vm list -g ${CLOUD_RESOURCE_GROUP} --query "[?contains(name, '$SUFFIX')].id" -o tsv)

for i in $list; do
   case $op in
      ls)
         cmd="az vm show --show-details --id $i --query '{ IP:publicIps, FQDN:fqdns, Type:hardwareProfile.vmSize , Power:powerState }' -o tsv"
         ;;
      start|stop|deallocate)
         cmd="az vm $op --no-wait --id $i"
         echo $cmd
         ;;
      delete)
         cmd="az vm delete --no-wait --yes --id $i"
         echo $cmd
         ;;
      *)
         echo Unknown operation \'$op\', skipping $(basename $i)
         ;;
   esac
   eval $cmd
done
