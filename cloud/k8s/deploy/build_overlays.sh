# Copyright 2022 Kontain
# Derived from:
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash
set -x

arg_count=$#
for arg in "$@"
do
   case "$arg" in
        --release-tag=*)
            tag="${1#*=}"
        ;;
    esac
    shift
done


RELEASE_TAG=$tag && envsubst < cloud/k8s/deploy/kontain-deploy/base/set_env.templ > cloud/k8s/deploy/kontain-deploy/base/set_env.yaml
# this file is only used for localized testing of kustomize outputs during edits to daemonset install
# mkdir -p ./kontain-deploy/outputs/
# kustomize build kontain-deploy/base > ./kontain-deploy/outputs/km.yaml
# kustomize build kontain-deploy/overlays/kkm  > ./kontain-deploy/outputs/kkm.yaml
# kustomize build kontain-deploy/overlays/km-crio  > ./kontain-deploy/outputs/km-crio.yaml
# kustomize build kontain-deploy/overlays/k3s  > ./kontain-deploy/outputs/k3s.yaml
