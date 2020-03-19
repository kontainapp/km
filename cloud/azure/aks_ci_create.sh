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
# Small script to create aks clusters.

readonly PROGNAME=$(basename $0)
readonly CUR_DIR=$(readlink -m $(dirname $0))
readonly CLUSTER_NAME=$1
readonly SP_APPID=$2
readonly SP_PASSWORD=$3

function usage() {
    cat <<- EOF
usage: $PROGNAME <CLUSTER_NAME> <SP_APPID> <SP_PASSWORD>

Program is a helper to launch new k8s cluster on aks.

EOF
}

function load_default_configs() {
    source ${CUR_DIR}/cloud_config.mk
    if [[ -z ${VM_SIZE} ]]; then
        readonly VM_SIZE=${K8S_NODE_INSTANCE_SIZE}
    fi
}

function create() {
    az aks create \
        --resource-group "${CLOUD_RESOURCE_GROUP}" \
        --name "$CLUSTER_NAME"  \
        --node-vm-size "$VM_SIZE" \
        --node-count 1 \
        --service-principal "${SP_APPID}" \
        --client-secret "${SP_PASSWORD}" \
        --generate-ssh-keys \
        --output ${OUT_TYPE}
	az aks get-credentials --resource-group "${CLOUD_RESOURCE_GROUP}" --name "${CLUSTER_NAME}" --overwrite-existing
}

function main() {
    if [[ -z $CLUSTER_NAME ]] | [[ -z $SP_APPID ]] | [[ -z $SP_PASSWORD ]]; then
        usage
        exit 1
    fi

    [ "$TRACE" ] && set -x

    load_default_configs
    create
}

main
