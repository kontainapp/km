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
# Clears all resources in resource group. Long running Bug hammer, will clean up all in RG.
# Be careful !
#
source `dirname $0`/cloud_config.mk

set -e
if [ -v BASH_TRACING ] ; then set -x ; fi
az account set -s ${CLOUD_SUBSCRIPTION}
az configure --defaults location=${CLOUD_LOCATION}}
az acr delete --resource-group ${CLOUD_RESOURCE_GROUP} --name  ${REGISTRY_NAME}
az group delete -y --name ${CLOUD_RESOURCE_GROUP}