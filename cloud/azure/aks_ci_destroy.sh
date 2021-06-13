#!/bin/bash -e
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