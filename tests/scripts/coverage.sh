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
# Script to run test coverage analysis

set -e ; [ "$TRACE" ] && set -x

readonly PROGNAME=$(basename $0)
readonly INPUT_SRC_DIR=$1
readonly INPUT_COVERAGE_SEARCH_DIR=$2
readonly OUTPUT_DIR=$3
readonly REPORT_VERSION=$4
if [[ -z ${REPORT_VERSION} ]]; then
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report"
else
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report - ${REPORT_VERSION}"
fi

readonly COVERAGE_CMD_NAME=gcovr
readonly COVERAGE_REPORT=${OUTPUT_DIR}/report.html
readonly PARALLEL=$(nproc --all)
if [[ -z ${MATCH} || ${MATCH} == '.*' ]]; then
    readonly COVERAGE_THRESHOLDS="--fail-under-branch 40  --fail-under-line 55"
fi

function usage() {
    cat <<- EOF
usage: $PROGNAME <INPUT_SRC_DIR> <INPUT_COVERAGE_SEARCH_DIR> <OUTPUT_DIR> <Optional REPORT_VERSION>

Run test coverage analysis.

EOF
    exit 1
}

function check_params() {
    if [[ -z ${INPUT_SRC_DIR} ]]; then usage; fi
    if [[ -z ${INPUT_COVERAGE_SEARCH_DIR} ]]; then usage; fi
    if [[ -z ${OUTPUT_DIR} ]]; then usage; fi
}

function main() {
    check_params

    if [[ ! -x $(command -v ${COVERAGE_CMD_NAME}) ]]; then
        echo "Error: ${COVERAGE_CMD_NAME} is not installed"
        exit 1
    fi

    ${COVERAGE_CMD_NAME} \
        --html-title "${REPORT_TITLE}" \
        --html \
        --html-details \
        ${COVERAGE_THRESHOLDS} \
        --root ${INPUT_SRC_DIR} \
        --output ${COVERAGE_REPORT} \
        ${INPUT_COVERAGE_SEARCH_DIR} \
        --print-summary \
        -j ${PARALLEL} \
        --exclude-unreachable-branches \
        --delete

    if [[ -f ${COVERAGE_REPORT} ]]; then
        echo "Report is located at ${COVERAGE_REPORT}"
    fi
}

main
