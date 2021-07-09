#
# Copyright 2021 Kontain Inc.
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
#
# config file. Used  from both makefiles and bash scripts, so no spaces before/after '='
#

# Tenant ID for kontain.app directory. 
AZ_TENANT_ID=bb863950-cdcd-467a-8d4c-97f338d8e279

# Azure groups all resources under this name
CLOUD_RESOURCE_GROUP=kontainKubeRG

# Azure payment reference (name or ID).
# Assumes the script authentication with Azure accoubt and subscription was crearted with this name
CLOUD_SUBSCRIPTION=Kontain-Pay-As-You-Go

# values from `az account list-locations`
CLOUD_LOCATION=westus

# Service Principle name, for convenience - alows to find the SP by name
K8_SERVICE_PRINCIPAL=http://kontainKubeSP

# Container registry name, usually passed to misc. commands
REGISTRY_NAME=kontainkubecr

# Server name for the registry, usualy a part of container tag needed for the push.
# Can be received with
#    az acr list --resource-group  ${CLOUD_RESOURCE_GROUP} \
#                 --query "[].{acrLoginServer:loginServer}" --output table
REGISTRY=${REGISTRY_NAME}.azurecr.io

# used in misc. messages
REGISTRY_AUTH_EXAMPLE="az acr login  -n ${REGISTRY_NAME}"

# how fancy we want the machine registry is running on
REGISTRY_SKU=Basic

# base cluster name. May be mangled by adding branch name etc... in different provision use cases
K8S_CLUSTER=kontainKubeCluster

# Used for each K8S node. Needs to support nested virtualization
K8S_NODE_INSTANCE_SIZE=Standard_D4s_v3

# default format for az command reporting info to tty
OUT_TYPE=table
