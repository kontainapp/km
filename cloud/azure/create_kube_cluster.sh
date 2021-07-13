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
# Deploys a kubernetes cluster instance to Azure.
# Also preps service instance for the cluster and configures it to access
# kontain private ContainerRegistry

# make sure we are running where we are started from (so the rest of the scripr can be run interactively)
cd `dirname $0`
# get all config vars
source ./cloud_config.mk
out_type=table

set -e ; [ "$TRACE" ] && set -x
# create service principal, give it access to ACR, create cluster with the SP
appPwd=`az ad sp create-for-rbac --name ${K8_SERVICE_PRINCIPAL} --skip-assignment --query password --output tsv`
echo Do not lose it PWD $appPwd . TODO: switch to pre-generated PEM certs
[ "$TRACE" ] && set -x
acrId=`az acr show --resource-group ${CLOUD_RESOURCE_GROUP} --name ${REGISTRY_NAME} --query "id" --output tsv`
sp_id=`az ad sp list  --query "[?contains(servicePrincipalNames,'$K8_SERVICE_PRINCIPAL')].{Id: objectId}" --all --output tsv`
appId=`az ad sp show --id ${sp_id}  --query appId --output tsv`
sleep 5 # give azure time to propagate the data. TODO - drop 'sleep' and make it wait on whatever is needed here
az role assignment create --assignee ${appId} --scope ${acrId} --role acrpull --output ${out_type}

az aks create --resource-group ${CLOUD_RESOURCE_GROUP}  --name ${K8S_CLUSTER}  \
   --node-vm-size ${K8S_NODE_INSTANCE_SIZE} --node-count 1 \
   --service-principal ${appId} --client-secret ${appPwd} \
    --generate-ssh-keys --output ${out_type}

# populate kubectl context for K8S_CLUSTER
# Note: if kubectl is not installed, azure has a helper 'az aks install-cli'
az aks get-credentials --resource-group ${CLOUD_RESOURCE_GROUP} --name ${K8S_CLUSTER} --overwrite-existing
