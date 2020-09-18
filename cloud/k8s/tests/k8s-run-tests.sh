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

[ "$TRACE" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CURRENT=$(readlink -m $(dirname $0))
readonly OP=$1
readonly TEST_IMAGE=$2
readonly TEST_NAME=$3
readonly TEST_COMMAND=$4

# We the RUNTIME_DIR to store the final result from processing the template.
# Using a /tmp directory so we can have a fresh record for every run.
readonly RUNTIME_DIR=$(mktemp -d)
readonly TEST_POD_TEMPLATE_NAME="test-pod-template.yaml"
readonly POD_WAIT_TIMEOUT=${K8S_POD_WAIT_TIMEOUT:-5m}

if [[ -z ${PIPELINE_WORKSPACE} ]]; then
    readonly GREEN=\\e[32m
    readonly NOCOLOR=\\e[0m
fi

function usage() {
    cat <<- EOF
usage: $PROGNAME <OP> <TEST_IMAGE> <TEST_NAME> <TEST_COMMAND>

Program is a helper to launch new tests on k8s cluster. Assume kubectl is
already configured.

OP:
    default                 Designed to run normal CI workflow.
    manual                  Designed to run the test, but not clean up the testenv container.
    err_no_cleanup          Designed to not clean up if there is error.

Example:

    To run normal CI:
    $PROGNAME default test-node-fedora:ci-nightly-1555 node-fedora-1555 script/test.sh test-all

    To run manually:
    $PROGNAME manual test-node-fedora:ci-nightly-1555 node-fedora-1555 script/test.sh test-all
EOF
}

function manual_usage {
    local pod_name=$1

    echo -e "Run bash in your pod '$pod_name' using '${GREEN}kubectl exec $pod_name -it -- bash${NOCOLOR}'"
    echo -e "Run tests inside your pod using '${GREEN}${TEST_COMMAND}${NOCOLOR}'"
    echo -e "When you are done, do not forget to '${GREEN}kubectl delete pod $pod_name${NOCOLOR}'"
    echo -e "Note: Deployment spec is written to '${GREEN}${RUNTIME_DIR}${NOCOLOR}'"
}

function process_op {
    case $OP in
    "manual")
        readonly MANUAL=1
        ;;
    "default")
        ;;
    "err_no_cleanup")
        readonly ERR_NO_CLEANUP=1
        ;;
    *)
        usage
        exit 1
        ;;
    esac
}

function check_bin {
    local bin_names=$@

    for bin_name in $bin_names
    do
        if [[ ! -x $(command -v ${bin_name}) ]]; then
            echo "Error: ${bin_name} is not installed"
            exit 1
        fi
    done
}

function prepare_template {
    echo "Using runtime dir ${RUNTIME_DIR}"
    m4 \
        -D NAME="${TEST_NAME}" \
        -D IMAGE="${TEST_IMAGE}" \
        ${CURRENT}/${TEST_POD_TEMPLATE_NAME} > ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}
}

function cleanup_and_exit {
    local pod_name=$1
    local error=$2

    kubectl logs pod/${pod_name}
    if [[ $error != 0 ]]; then
        kubectl describe pod ${pod_name}
        kubectl get pod/${pod_name} -o json
    fi

    if [[ $error != 0 ]] && [[ -n $ERR_NO_CLEANUP ]]; then
        echo "Won't clean up on error"
        exit $error
    fi

    kubectl delete -f ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}
    rm -rf ${RUNTIME_DIR}
    exit $error
}

function main {
    process_op
    check_bin kubectl m4
    prepare_template

    local pod_name=$(kubectl create -f ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME} -o jsonpath='{.metadata.name}')

    # For manual, we don't run the test automatically. Users can examine the
    # testenv container manually.
    if [[ -n $MANUAL ]]; then
        manual_usage $pod_name
        exit 0
    fi

    kubectl wait pod/${pod_name} --for=condition=Ready --timeout=${POD_WAIT_TIMEOUT}
    local exit_code=$?
    if [[ $exit_code != 0 ]]; then
        echo "Failed to launch test pod: ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}"
        cleanup_and_exit $pod_name $exit_code
    fi

    kubectl exec ${pod_name} -- bash -c "${TEST_COMMAND}"
    local exit_code=$?
    if [[ $exit_code != 0 ]]; then
        echo "Failed to run command: ${TEST_COMMAND}"
    fi
    cleanup_and_exit $pod_name $exit_code
}

main