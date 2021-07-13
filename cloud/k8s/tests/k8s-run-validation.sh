#!/bin/bash
#
#  Copyright © 2018-2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# This script is used for validation logic for runenv images on k8s.

[ "$TRACE" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CURRENT=$(readlink -m $(dirname $0))
readonly TEST_IMAGE="$1"
readonly TEST_NAME="$2"
readonly TEST_ARGS="$3"
readonly TEST_EXPECTED="$4"

# We the RUNTIME_DIR to store the final result from processing the template.
# Using a /tmp directory so we can have a fresh record for every run.
readonly RUNTIME_DIR=$(mktemp -d)
readonly TEST_POD_TEMPLATE_NAME="validate-pod-template.yaml"
readonly POD_WAIT_TIMEOUT=${K8S_POD_WAIT_TIMEOUT:-120}
readonly POD_POLL_WAIT=3

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
    echo "Creating template for ${TEST_NAME} IMAGE=${TEST_IMAGE} runtime dir ${RUNTIME_DIR} spec ${TEST_POD_TEMPLATE_NAME}"
    echo "                 ARGS=${TEST_ARGS} EXPECTED=${TEST_EXPECTED@Q}"
    m4 \
        -D NAME="${TEST_NAME}" \
        -D IMAGE="${TEST_IMAGE}" \
        -D ARGS="${TEST_ARGS}" \
        ${CURRENT}/${TEST_POD_TEMPLATE_NAME} > ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}
    echo Pod Template:
    cat ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}
    echo -e \\nEnd of Pod Template
}

function poll_pod {
   for i in $(seq 0 ${POD_POLL_WAIT} ${POD_WAIT_TIMEOUT}) ; do
      reason=$(kubectl get -o jsonpath='{.status.containerStatuses[].state.terminated.reason}' pod/${1})
      echo "Trying... seq=$i (step is ${POD_POLL_WAIT}) - reason = '$reason'"
      case ${reason} in
         Error)
            return 2
            ;;
         Completed)
            return 0
            ;;
         *)
            sleep ${POD_POLL_WAIT}
            ;;
      esac
   done
   return 1
}

function main {
    check_bin kubectl m4
    prepare_template

    local pod_name=$(kubectl create -f ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME} -o jsonpath='{.metadata.name}')

    poll_pod ${pod_name}
    local exit_code=$?
    if [[ $exit_code != 0 ]]; then
       kubectl describe pod ${pod_name}
       kubectl get pod/${pod_name} -o json
       kubectl logs ${pod_name}
    else
       kubectl logs ${pod_name} | grep "${RUNENV_VALIDATE_EXPECTED}"
       exit_code=$?
    fi

    kubectl delete -f ${RUNTIME_DIR}/${TEST_POD_TEMPLATE_NAME}
    rm -rf ${RUNTIME_DIR}
    exit $exit_code
}

main
