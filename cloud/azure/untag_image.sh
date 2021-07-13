#!/bin/bash
#
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
# untags individual image in Azure COntainer registry.
# ImageName Version are passed in $1 adnd $2
#
# Note that initial push/tag is happening via 'docker push' which is cloud-independent, so there is
# no script for it in ./
#
source `dirname $0`/cloud_config.mk
# DEBUG=echo
out_type=table

image="$1"
version="$2"
if [ -z "$image" -o -z "$version" ] ; then echo Usage: $0 image_short_name version; exit; fi

[ "$TRACE" ] && set -x
$DEBUG az acr login -n ${REGISTRY_NAME}
$DEBUG az acr repository show-tags --repository ${image} -n ${REGISTRY_NAME} --output ${out_type}
$DEBUG az acr repository untag  -n ${REGISTRY_NAME}  --image ${image}:${version}
