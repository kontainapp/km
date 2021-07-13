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
# Clears all resources in resource group. Long running Bug hammer, will clean up all in RG.
# Be careful !
#
source `dirname $0`/cloud_config.mk

set -e ; [ "$TRACE" ] && set -x
az account set -s ${CLOUD_SUBSCRIPTION}
az configure --defaults location=${CLOUD_LOCATION}}
az acr delete --resource-group ${CLOUD_RESOURCE_GROUP} --name  ${REGISTRY_NAME}
az group delete -y --name ${CLOUD_RESOURCE_GROUP}
