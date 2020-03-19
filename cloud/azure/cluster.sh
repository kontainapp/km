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
# Small script to create and destroy k8s clusters in aks.

readonly PROGNAME=$(basename $0)
readonly CUR_DIR=$(readlink -m $(dirname $0))
readonly OPT=$1
readonly CLUSTER_NAME=$2
readonly SERVICE_PRINCIPAL_NAME="https://aks-${CLUSTER_NAME}-sp"
readonly ARGS="${@:3}"

function usage() {
    cat <<- EOF
usage: $PROGNAME opt <CLUSTER_NAME> options

Program is a helper to launch new k8s cluster on aks.

OPT:
    create                   create a new k8s cluster
    clear                    remove a new k8s cluster

OPTIONS:
    -s --vmsize              Override the default size of VM used
    -h --help                show this help
    -x --debug               show debug trace
EOF
}

function get_service_principal() {
    readonly SERVICE_PRINCIPAL_PASSWORD=$(az ad sp create-for-rbac --name $SERVICE_PRINCIPAL_NAME --skip-assignment --query password --output tsv)
    local sp_id=$(az ad sp list --all --query "[?contains(servicePrincipalNames,'$SERVICE_PRINCIPAL_NAME')].{Id: objectId}"  --output tsv)
    readonly SERVICE_PRINCIPAL_APP_ID=$(az ad sp show --id $sp_id  --query appId --output tsv)

    local acr_id=$(az acr show --resource-group ${CLOUD_RESOURCE_GROUP} --name ${REGISTRY_NAME} --query "id" --output tsv)
    az role assignment create --assignee ${SERVICE_PRINCIPAL_APP_ID} --scope ${acr_id} --role acrpull
}

function remove_service_principal() {
    local sp_id=$(az ad sp list --all --query "[?contains(servicePrincipalNames,'$SERVICE_PRINCIPAL_NAME')].{Id: objectId}"  --output tsv)
    az ad sp delete --id $sp_id
}

function create() {
    az aks create \
        --resource-group ${CLOUD_RESOURCE_GROUP} \
        --name "$CLUSTER_NAME"  \
        --node-vm-size "$VM_SIZE" \
        --node-count 1 \
        --service-principal ${SERVICE_PRINCIPAL_APP_ID} \
        --client-secret ${SERVICE_PRINCIPAL_PASSWORD} \
        --generate-ssh-keys \
        --output ${OUT_TYPE}
	az aks get-credentials --resource-group $(CLOUD_RESOURCE_GROUP) --name $(CLUSTER_NAME) --overwrite-existing
}

function clear() {
    az aks delete -y --resource-group ${CLOUD_RESOURCE_GROUP}  --name "$CLUSTER_NAME" --output ${OUT_TYPE}
}

function cmdline() {
    local arg=
    for arg
    do
        local delim=""
        case "$arg" in
            # translate --gnu-long-options to -g (short options)
            --vmsize)         args="${args}-s ";;
            --help)           args="${args}-h ";;
            --debug)          args="${args}-x ";;
            # pass through anything else
            *) [[ "${arg:0:1}" == "-" ]] || delim="\""
                args="${args}${delim}${arg}${delim} ";;
        esac
    done

    # Reset the positional parameters to the short options
    eval set -- $args

    while getopts "s:hx" OPTION
    do
         case $OPTION in
         h)
             usage
             exit 0
             ;;
         s)
             VM_SIZE=$OPTARG
             ;;
         x)
            DEBUG=1
            ;;
        esac
    done
}

function load_default_configs() {
    source ${CUR_DIR}/cloud_config.mk
    VM_SIZE=${K8S_NODE_INSTANCE_SIZE}
}

function main() {
    if [[ -z OPT ]]; then
        usage
        exit 1
    fi

    if [[ -z CLUSTER_NAME ]]; then
        usage
        exit 1
    fi
    # Orders are important. Load default first, then parse cmdline args to see
    # if there is any override.
    load_default_configs
    cmdline $ARGS

    [[ -n DEBUG ]] && set -x

    case $OPT in
    create)
        get_service_principal
        create
        ;;
    clear)
        clear
        remove_service_principal
        ;;
    *)
        usage
        exit 1
        ;;
    esac
}

main