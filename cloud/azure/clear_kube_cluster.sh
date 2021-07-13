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
# Deletes the Kubernetes cluster and the Service Principal it used to use

# make sure we are running where we are started from (so the rest of the scripr can be run interactively)
cd `dirname $0`
# get all config vars
source ./cloud_config.mk

#set -e  # even if some resource deletion fails, keep deleting others. Thus no need for 'set -e' here
[ "$TRACE" ] && set -x
sp_id=`az ad sp list  --query "[?contains(servicePrincipalNames,'$K8_SERVICE_PRINCIPAL')].{Id: objectId}"  --output tsv`
az aks delete --no-wait --yes --resource-group ${CLOUD_RESOURCE_GROUP}  --name ${K8S_CLUSTER}
az ad sp delete --id $sp_id
kubectl config delete-context ${K8S_CLUSTER}
