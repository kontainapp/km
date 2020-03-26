#!/bin/bash
#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

readonly PROGNAME=$(basename $0)
readonly CURRENT=$(readlink -m $(dirname $0))
readonly OP=$1
readonly TEST_IMAGE=$2
readonly TEST_SUFFIX=$3
readonly TEST_COMMAND=$4

readonly RUNTIME_DIR=$(mktemp -d)
readonly TEST_POD_TEMPLATE_NAME="test-pod-template.yaml"
readonly GREEN=\\e[32m
readonly NOCOLOR=\\e[0m

function usage() {
    cat <<- EOF
usage: $PROGNAME <OP> <TEST_IMAGE> <TEST-SUFFIX> <TEST_COMMAND>

Program is a helper to launch new tests on k8s cluster. Assume kubectl is
already configured.

EOF
}

function manual_usage {
    local pod_name=$1

    echo -e "Run bash in your pod '$pod_name' using '${GREEN}kubectl exec $pod_name -it -- bash${NOCOLOR}'"
    echo -e "Run tests inside your pod using '${GREEN}${TEST_COMMAND}${NOCOLOR}'"
    echo -e "When you are done, do not forget to '${GREEN}kubectl delete pod $pod_name${NOCOLOR}'"
}

function process_op {
    case $OP in
    "manual")
        readonly MANUAL=1
        ;;
    "default")
        ;;
    "no_cleanup")
        readonly NO_CLEANUP=1
        ;;
    *)
        usage
        exit 1
        ;;
    esac
}

function check_bin {
    local bin_name=$1
    if ! [ -x "$(command -v ${bin_name})" ]; then
        echo "Error: ${bin_name} is not installed"
        exit 1
    fi
}

function prepare_kustomize {
    echo "Using runtime dir ${RUNTIME_DIR}"
    cp ${CURRENT}/${TEST_POD_TEMPLATE_NAME} ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}
    cd ${RUNTIME_DIR}; kustomize create --resources ./${TEST_POD_TEMPLATE_NAME}
    cd ${RUNTIME_DIR}; kustomize edit set image test-image=${TEST_IMAGE} 
    cd ${RUNTIME_DIR}; kustomize edit set namesuffix -- "-${TEST_SUFFIX}"
}

function cleanup {
    if [ $MANUAL ]; then
        manual_usage $pod_name
        return
    fi

    if ! [ $NO_CLEANUP ]; then
        kubectl delete -k ${RUNTIME_DIR}
        rm -rf ${RUNTIME_DIR}
        return
    fi
}

function main {
    [ "${TRACE}" ] && set -x

    process_op

    check_bin kustomize
    check_bin kubectl
    prepare_kustomize

    local pod_name=$(kubectl create -k ${RUNTIME_DIR} -o jsonpath='{.metadata.name}')

    kubectl wait pod/${pod_name} --for=condition=Ready --timeout=2m
    if ! [ "$?" = "0" ]; then
        kubectl get ${pod_name} -o json
        exit 1
    fi

    kubectl exec -it ${pod_name} -- ${TEST_COMMAND}
    if [ "$?" = "0" ]; then
        cleanup
    else
        cleanup
        exit 1
    fi
}

main