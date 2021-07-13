#!/bin/bash
#
#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Run km test coverage on k8s

[ "$TRACE" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CURRENT=$(readlink -m $(dirname $0))

readonly TEST_IMAGE=$1
readonly TEST_NAME=$2
readonly TEST_COMMAND=$3
readonly HOST_COVERAGE_DEST=$4

# We the RUNTIME_DIR to store the final result from processing the template.
# Using a /tmp directory so we can have a fresh record for every run.
readonly RUNTIME_DIR=$(mktemp -d)
readonly TEST_POD_TEMPLATE_NAME="test-pod-template.yaml"
readonly POD_WAIT_TIMEOUT=${K8S_POD_WAIT_TIMEOUT:-5m}

function usage() {
    cat <<- EOF
usage: $PROGNAME <TEST_IMAGE> <TEST_NAME> <TEST_COMMAND> <HOST_COVERAGE_DEST>

Program is a helper to run coverage tests on k8s cluster. Assume kubectl is
already configured.
EOF
    exit 1
}

function check_variables {
    if [[ -z $TEST_IMAGE ]]; then usage; fi
    if [[ -z $TEST_NAME ]]; then usage; fi
    if [[ -z $TEST_COMMAND ]]; then usage; fi
    if [[ -z $HOST_COVERAGE_DEST ]]; then usage; fi
}

function check_bin {
    local bin_names=$@

    for bin_name in $bin_names; do
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

    kubectl delete -f ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}
    rm -rf ${RUNTIME_DIR}
    exit $error
}

# creates a pod for coverage test run and executes test run there. Then copies
# coverage data back to host for coverage analysis
function main {
    check_variables
    check_bin kubectl m4
    prepare_template

    local pod_name=$(kubectl create -f ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME} -o jsonpath='{.metadata.name}')

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
        cleanup_and_exit $pod_name $exit_code
    fi

    # The output .gcda files are inside the container after tests are run, but
    # to run coverage analysis on these files, we need access to the km source
    # code, which are not provided inside testenv. Therefore, we need to cp
    # coverage file out of pod onto the host.
    kubectl cp ${pod_name}:/home/appuser/km/build/km/coverage/. ${HOST_COVERAGE_DEST}/.
    local exit_code=$?
    if [[ $exit_code != 0 ]]; then
        echo "Failed to cp coverage output: ${HOST_COVERAGE_DEST}"
    fi
    cleanup_and_exit $pod_name $exit_code
}

main
