#!/bin/bash
# Copyright © 2018 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Deletes the Kubernetes cluster and the Service Principal it used to use

# make sure we are running where we are started from (so the rest of the scripr can be run interactively)
cd `dirname $0`
# get all config vars
source ./cloud_config.mk

#set -e  # even if some resource deletion fails, keep deleting others. Thus no need for 'set -e' here
set -x
sp_id=`az ad sp list  --query "[?contains(servicePrincipalNames,'$K8_SERVICE_PRINCIPAL')].{Id: objectId}"  --output tsv`
az aks delete -y --resource-group ${CLOUD_RESOURCE_GROUP}  --name ${K8S_CLUSTER}
az ad sp delete --id $sp_id
kubectl config delete-context ${K8S_CLUSTER}