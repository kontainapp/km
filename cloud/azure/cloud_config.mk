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
#
# config file. Used  from both makefiles and bash scripts, so no spaces before/after '='
#

# Azure groups all resources under this name
CLOUD_RESOURCE_GROUP=kontainKubeRG

# Azure payment reference (name or ID).
# Assumes the script authentication with Azure accoubt and subscription was crearted with this name
CLOUD_SUBSCRIPTION=Pay-As-You-Go

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
K8S_NODE_INSTANCE_SIZE=Standard_D2s_v3

