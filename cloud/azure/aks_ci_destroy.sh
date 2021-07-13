#!/bin/bash -e
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
# Small script to destroy aks clusters.

readonly PROGNAME=$(basename $0)
readonly CUR_DIR=$(readlink -m $(dirname $0))
readonly CLUSTER_NAME=$1

function usage() {
    cat <<- EOF
usage: $PROGNAME <CLUSTER_NAME>

Program is a helper to launch new k8s cluster on aks.

EOF
}

function load_default_configs() {
    source ${CUR_DIR}/cloud_config.mk
}

function destroy() {
    az aks delete --no-wait --yes --resource-group "${CLOUD_RESOURCE_GROUP}"  --name "${CLUSTER_NAME}" --output ${OUT_TYPE}
}

function main() {
    if [[ -z $CLUSTER_NAME ]]; then
        usage
        exit 1
    fi

    [ "$TRACE" ] && set -x

    load_default_configs
    destroy
}

main