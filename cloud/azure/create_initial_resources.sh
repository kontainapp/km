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
# Creates all resources needed before we can create a K8s cluster

source `dirname $0`/cloud_config.mk

set -e
set -x
az account set -s ${CLOUD_SUBSCRIPTION}
az configure --defaults location=${CLOUD_LOCATION}
az group create --name ${CLOUD_RESOURCE_GROUP} --output ${OUT_TYPE}
az acr create \
   --resource-group ${CLOUD_RESOURCE_GROUP} \
   --name ${REGISTRY_NAME} --sku ${REGISTRY_SKU} --output ${OUT_TYPE}
