#!/bin/bash -e
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

readonly PROGNAME=$(basename "$0")
readonly CURRENT=$(readlink -m $(dirname "$0"))
readonly OP="$1"
readonly TEST_IMAGE="$2"
readonly TEST_NAME="$3"
readonly TEST_COMMAND="$4"

# script name for messages
readonly self="$(basename ${BASH_SOURCE[0]})"

# We generate spec from template and save it in a dedicated (to this test run) RUNTIME_DIR
readonly RUNTIME_DIR=$(mktemp -d)
readonly POD_SPEC_TEMPLATE="test-pod-template.yaml"
readonly POD_SPEC="${RUNTIME_DIR}/${POD_SPEC_TEMPLATE}"
readonly POD_WAIT_TIMEOUT=${K8S_POD_WAIT_TIMEOUT:-5m}

if [[ -z ${PIPELINE_WORKSPACE} ]]; then
    readonly GREEN=\\e[32m
    readonly NOCOLOR=\\e[0m
fi

function usage() {
    cat <<- EOF
usage: $PROGNAME <OP> <TEST_IMAGE> <TEST_NAME> <TEST_COMMAND>

A helper to launch tests on k8s cluster. Assumes kubectl is
already configured to authenticate to the correct cluster.

OP:
    default                 Normal CI workflow - prepare pod, run tests, clean up pod
    err_no_cleanup          Normal CI workflow, but do not clean up on error so it can be investigated
    manual                  Prepare pod and tell use how to run the test

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
    echo -e "Note: Deployment spec is written to '${GREEN}${POD_SPEC}${NOCOLOR}'"
}

function process_op {
    case "$OP" in
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
   for name in "$@"
   do
     if [[ ! -x $(command -v "${name}") ]]; then
         echo "$self: Error: ${name} is not installed";
         return 1
    fi
   done
}


function prepare_template {
    echo $self: Generating ${POD_SPEC} with \
         "TEST_NAME='${TEST_NAME}' TEST_IMAGE='${TEST_IMAGE}', TEST_COMMAND='${TEST_COMMAND}'"
    m4 \
        -D NAME="${TEST_NAME}" \
        -D IMAGE="${TEST_IMAGE}" \
        ${CURRENT}/${POD_SPEC_TEMPLATE} > ${POD_SPEC}
    echo ========== Pod Spec ${POD_SPEC}:
    cat ${POD_SPEC}
    echo ==========
}

function cleanup_and_exit {
    local pod_name=$1
    local error=$2

    echo "=== Run logs from pod/${pod_name}: error=$error"
    kubectl logs pod/${pod_name}
    echo "==="

    if [[ $error != 0 ]]; then
      echo "=== Failed run. Pod information:"
      echo "=== describe pod:"
      kubectl describe pod ${pod_name}
      echo "=== get pod:"
      kubectl get pod/${pod_name} -o json
      echo "==="

    fi

    if [[ "$error" == 0  ||  "$ERR_NO_CLEANUP" != 1  ]] ; then
      echo "$self: Cleaning up. Error=$error. ERR_NO_CLEANUP='$ERR_NO_CLEANUP'"
      kubectl delete --wait=false -f ${POD_SPEC}
      rm -rf ${RUNTIME_DIR}
    fi

    exit $error
}

function main {
    process_op
    check_bin kubectl m4
    prepare_template

    local pod_name # separate declaration to avoid masking failures
    pod_name=$(kubectl create -f ${POD_SPEC} -o jsonpath='{.metadata.name}')
    trap "cleanup_and_exit ${pod_name} 0" SIGINT

    # For manual mode, just tell user how
    # to run the test and how to clean up the results.
    if [[ "$MANUAL" == 1 ]]; then
        manual_usage $pod_name
        exit 0
    fi

    # We assume pod takes non-zero time to run and will not complete before we enter this wait.
    if ! kubectl wait pod/${pod_name} --for=condition=Ready --timeout=${POD_WAIT_TIMEOUT} ; then
        echo "$self: Failed to launch pod. Timeout=${POD_WAIT_TIMEOUT}"
        cleanup_and_exit $pod_name 1
    fi

    echo "$self: kubectl exec  ${pod_name} -- bash -c \"${TEST_COMMAND}\""
    if ! kubectl exec ${pod_name} -- bash -c "${TEST_COMMAND}" ; then
        echo "$self: Failed to run command '${TEST_COMMAND}'"
        cleanup_and_exit $pod_name 2
    fi
    cleanup_and_exit $pod_name 0
}

main