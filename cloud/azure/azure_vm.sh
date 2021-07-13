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
# A helper script to launch VM for debugging purposes.
#
set -e ; [ "$TRACE" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CUR_DIR=$(readlink -m $(dirname $0))
readonly OP=$1
readonly VM_NAME=$2

source ${CUR_DIR}/cloud_config.mk
readonly RESOURCE_GROUP=${RESOURCE_GROUP:-"${USER}-rg"}
readonly ADMIN=${ADMIN:-"kontain"}
readonly VM_SIZE=${VMSIZE:-${K8S_NODE_INSTANCE_SIZE}}
# See list: az vm image list --output table
readonly DEFAULT_VM="Canonical:UbuntuServer:18.04-LTS:latest"
readonly VM_IMAGE=${3:-${VM_IMAGE:-${DEFAULT_VM}}}
readonly QUERY='{ IP:publicIps, FQDN:fqdns, Type:hardwareProfile.vmSize , Power:powerState }'

function usage() {
    cat <<- EOF
usage: $PROGNAME <OP> <VM_NAME>

Program is a helper to launch new azure vm.

OPS:
  start|stop|deallocate|ip|create vm-name [image] - operation on a vm
  ls - list all vms in the account (sans Kubernetes node pool)


Options (pass as env. vars):
    RESOURCE_GROUP         Resource group. default: ${USER}-RESOURCE_GROUP
    ADMIN      Admin user. default: kontain
    VM_SIZE    Vm size (type). default: ${K8S_NODE_INSTANCE_SIZE}
    VM_IMAGE   Base image to use. Same as passing extra aRESOURCE_GROUP. default: ${DEFAULT_VM}
EOF
    exit 1
}

set -x
case $OP in
    create)
        az vm create \
            --resource-group ${RESOURCE_GROUP} \
            --name ${VM_NAME} \
            --public-ip-address-dns-name ${VM_NAME} \
            --image ${VM_IMAGE} \
            --size ${VM_SIZE} \
            --admin-username ${ADMIN} \
            --query "${QUERY}" \
            --output tsv
        ;;
    ip)
        az vm show \
            --show-details \
            --resource-group ${RESOURCE_GROUP} \
            --name ${VM_NAME} \
            --query "${QUERY}" \
            -o tsv
        ;;
    ls)
      az vm list -o table -d
      ;;
    start|stop|deallocate)
        az vm $OP \
            --no-wait \
            --resource-group ${RESOURCE_GROUP} \
            --name ${VM_NAME}
        ;;
    delete)
        az vm delete --no-wait --yes \
            --resource-group ${RESOURCE_GROUP} \
            --name ${VM_NAME}
        ;;
    *)
        echo Unknown operation \'$OP\'
        usage
        ;;
esac
