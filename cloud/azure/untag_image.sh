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
