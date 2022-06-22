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
# Script to consolidate coverage reports from multiple runs and convert to HTML

set -e ; [ "$TRACE" ] && set -x

# usage coverage-report <json report(s) location> <src root path> <output-directory> <report-version>

readonly PROGNAME=$(basename $0)
readonly INPUT_SRC_DIR=$1
readonly REPORTS_DIR=$2
readonly OUTPUT_DIR=$3
readonly REPORT_VERSION=$4

readonly COVERAGE_CMD_NAME=gcovr
readonly COVERAGE_REPORT=${OUTPUT_DIR}/report.html
readonly COVERAGE_TRACEFILES=${OUTPUT_DIR}/*.json

function usage() {
    cat <<- EOF
usage: $PROGNAME <INPUT_SRC_DIR> <REPORTS_DIR> <OUTPUT_DIR> <Optional REPORT_VERSION>

Generate HTML coverage report.

All report .json files must be located in <REPORTS_DIR>
<INPUT_SRC_DIR> - Directory containing sourse files for the report

EOF
    exit 1
}

if [[ -z ${REPORT_VERSION} ]]; then
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report"
else
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report - ${REPORT_VERSION}"
fi

function check_params() {
    if [[ -z ${REPORTS_DIR} ]]; then usage; fi
    if [[ -z ${INPUT_SRC_DIR} ]]; then usage; fi
    if [[ -z ${OUTPUT_DIR} ]]; then usage; fi

    if ! command -v ${COVERAGE_CMD_NAME} &> /dev/null ; then
        echo "Error: ${COVERAGE_CMD_NAME} is not installed"
        exit 1
    fi
}

function main() {
    check_params

   ${COVERAGE_CMD_NAME} \
      --html-title "${REPORT_TITLE}" \
      --html \
      --html-details \
      --add-tracefile ${COVERAGE_TRACEFILES} \
      --root ${INPUT_SRC_DIR} \
      --output ${COVERAGE_REPORT}

   if [[ -f ${COVERAGE_REPORT} ]]; then
      echo "Report is located at ${COVERAGE_REPORT}"
   fi
}

main
