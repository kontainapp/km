#!/bin/bash -e
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
# Small script to create aks clusters.

readonly PROGNAME=$(basename $0)
readonly CUR_DIR=$(readlink -m $(dirname $0))
readonly CLUSTER_NAME=$1

if [ -z "$SP_APPID" -o -z "$SP_PASSWORD"]; then
   echo Please set SP_APPID and SP_PASSWORD env. vars to access Azure
   false
fi

function usage() {
    cat <<- EOF
A helper to launch new k8s cluster on aks.

usage: $PROGNAME <CLUSTER_NAME>

env vars SP_APPID and SP_PASSWORD are expected for authentication with Azure.
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
