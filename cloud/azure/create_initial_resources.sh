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
# Creates all resources needed before we can create a K8s cluster
# Gets all config from cloud_config.mk
source `dirname $0`/cloud_config.mk
out_type=table

set -x
az account set -s ${CLOUD_SUBSCRIPTION}
az configure --defaults location=${CLOUD_LOCATION}
az group create --name ${CLOUD_RESOURCE_GROUP} --output ${out_type}
az acr create \
   --resource-group ${CLOUD_RESOURCE_GROUP} \
   --name ${REGISTRY_NAME} --sku ${REGISTRY_SKU} --output ${out_type}
