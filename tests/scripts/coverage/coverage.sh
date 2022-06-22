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
readonly REPORT_NAME=${4:-report}

if [[ -z ${REPORT_VERSION} ]]; then
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report"
else
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report - ${REPORT_VERSION}"
fi

readonly COVERAGE_CMD_NAME=gcovr
readonly COVERAGE_REPORT=${OUTPUT_DIR}/${REPORT_NAME}.json
readonly PARALLEL=$(nproc --all)

function usage() {
    cat <<- EOF
usage: $PROGNAME <INPUT_SRC_DIR> <INPUT_COVERAGE_SEARCH_DIR> <OUTPUT_DIR> <Optional REPORT_NAME>

Run test coverage analysis. Will geberate .json file. Use coverage-report.sh to generate HTML reports.

EOF
    exit 1
}

function check_params() {
   if [[ -z ${INPUT_SRC_DIR} ]]; then usage; fi
   if [[ -z ${INPUT_COVERAGE_SEARCH_DIR} ]]; then usage; fi
   if [[ -z ${OUTPUT_DIR} ]]; then usage; fi

   if ! command -v ${COVERAGE_CMD_NAME} &> /dev/null ; then
   #  if [[ ! -x $(command -v ${COVERAGE_CMD_NAME}) ]]; then
      echo "Error: ${COVERAGE_CMD_NAME} is not installed"
      exit 1
   fi

}

function main() {
    check_params

   ${COVERAGE_CMD_NAME} \
      --json \
      --root ${INPUT_SRC_DIR} \
      --output ${COVERAGE_REPORT} \
      -j ${PARALLEL} \
      --exclude-unreachable-branches \
      --delete \
      ${INPUT_COVERAGE_SEARCH_DIR}
}

main
