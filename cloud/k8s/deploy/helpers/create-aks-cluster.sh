# Copyright 2021 Kontain
# Derived from:
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash

[ "$TRACE" ] && set -x

region=us-west-1
cleanup_only=''
arg_count=$#

print_help() {
    echo "usage: $0  [options] prefix"
    echo "Creates AKS cluster with the name <prefix>-aks-cluster. All other associated recource names are prefixes with <prefix> "
    echo ""
    echo "Prerequisites:"
    echo "  AZURE CLI"
    echo ""
    echo "-h,--help print this help"
    echo "--tenant AZURE tenant ID"
    echo "--app-id AZURE app id"
    echo "--password AZURE secret or password"
    echo "--region Sets aws region. Default to us-west-1"
    echo "--cleanup Instructs script to delete cluster and all related resourses "
    exit 0
}

main() {

	az login --service-principal -u "${app_id}" -p "${pwd}" --tenant "${tenant_id}" -o table

    az group create --name ${resource_group_name} --location westus3
    # To access the nodes at the address created by `--enable-node-public-ip` need to open ssh port on the corresponding sg
    az aks create -g ${resource_group_name} -n ${cluster_name} --enable-managed-identity --node-count 1 --node-vm-size Standard_D4s_v5 --enable-node-public-ip

    # Configure kubectl
    az aks get-credentials --resource-group ${resource_group_name} --name ${cluster_name} --overwrite-existing
}

do_cleanup() {
    echo "deleting cluster"
    az login --service-principal -u "${app_id}" -p "${pwd}" --tenant "${tenant_id}" -o table

    az aks delete --name ${cluster_name} --resource-group ${resource_group_name} --yes 
    az group delete --name ${resource_group_name} --yes
}

for arg in "$@"
do
   case "$arg" in
        --tenant=*)
            tenant_id="${1#*=}"
            arg_count=$((arg_count-1))
        ;;
        --app-id=*)
            app_id="${1#*=}"
            arg_count=$((arg_count-1))
        ;;
        --password=*)
            pwd="${1#*=}"
            arg_count=$((arg_count-1))
        ;;
        --region=*)
            region="${1#*=}"
        ;;
        --cleanup)
            cleanup='yes'
        ;;
        --help | -h)
            print_help
        ;;
        -* | --*)
            echo "unknown option ${1}"
            print_help
        ;; 
        *)
            if [[ -z $prefix ]]; then 
                prefix=$arg 
                arg_count=$((arg_count-1))
            else
                echo "Too many arguments"
                print_help
            fi
        ;;
    esac
    shift
done

if [ -z $prefix ];then
    echo "Prefix is equired "
    exit 1
fi

if [ -z $app_id ];then
    echo "Application id is equired "
    exit 1
fi

if [ -z $tenant_id ];then
    echo "Tenant id is equired "
    exit 1
fi

if [ -z $pwd ];then
    echo "password (secret) is equired "
    exit 1
fi

readonly resource_group_name=${prefix}-resource_group
readonly cluster_name=${prefix}-cluster

if [ ! -z $cleanup ] && [ $arg_count == 1 ]; then
    do_cleanup
    exit
fi

main

#clean at the end if requested
if [ ! -z $cleanup ]; then 
    do_cleanup
fi

