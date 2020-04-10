#!/bin/bash -ex
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
# Script to run coverage using gcovr

readonly PROGNAME=$(basename $0)
readonly INPUT_SRC_DIR=$1
readonly INPUT_COVERAGE_SEARCH_DIR=$2
readonly OUTPUT_DIR=$3

readonly COVERAGE_CMD_NAME=gcovr
readonly COVERAGE_THRESHOLDS="--fail-under-branch 40  --fail-under-line 55"
readonly COVERAGE_REPORT=${OUTPUT_DIR}/report.html
readonly PARALLEL=$(nproc --all)

function usage() {
    cat <<- EOF
usage: $PROGNAME <INPUT_SRC_DIR> <INPUT_COVERAGE_SEARCH_DIR> <OUTPUT_DIR> 

EOF
    exit 1
}

function check_params() {
    if [[ -z ${INPUT_SRC_DIR} ]]; then
        usage
    fi
    if [[ -z ${INPUT_COVERAGE_SEARCH_DIR} ]]; then
        usage
    fi
    if [[ -z ${OUTPUT_DIR} ]]; then
        usage
    fi
}

function main() {
    check_params

    if [[ ! -x $(command -v ${COVERAGE_CMD_NAME}) ]]; then
        echo "Error: ${COVERAGE_CMD_NAME} is not installed"
        exit 1
    fi

    ${COVERAGE_CMD_NAME} \
        ${COVERAGE_THRESHOLDS} \
        --root ${INPUT_SRC_DIR} \
        --html \
        --html-details \
        --output ${COVERAGE_REPORT} \
        ${INPUT_COVERAGE_SEARCH_DIR} \
        --html-title "Kontain Monitor Code Coverage Report" \
        --print-summary \
        -j ${PARALLEL} \
        --exclude-unreachable-branches \
        --delete
}

main
