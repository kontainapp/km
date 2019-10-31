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
# Operates on existing Vm instance with openwhisk-kontain suffix in the name.
# 1st arg is operation. See Usage below for more info
#
source `dirname $0`/cloud_config.mk
SUFFIX='openwhisk-kontain'

# set -x
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
