#!/bin/bash
#
# Copyright 2022 Kontain Inc
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

if [[ -z ${REPORT_VERSION} ]]; then
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report"
else
    readonly REPORT_TITLE="Kontain Monitor Code Coverage Report - ${REPORT_VERSION}"
fi

readonly COVERAGE_CMD_NAME=gcovr
readonly COVERAGE_REPORT=${OUTPUT_DIR}/report.html
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
      echo "Error: ${COVERAGE_CMD_NAME} is not installed"
      exit 1
   fi

}

function main() {
    check_params

   #generate json report to be combined
   SUMMARY=$(${COVERAGE_CMD_NAME} \
      --html \
      --html-details \
      --root ${INPUT_SRC_DIR} \
      --output ${COVERAGE_REPORT} \
      -j ${PARALLEL} \
      --exclude-unreachable-branches \
      --print-summary \
      ${INPUT_COVERAGE_SEARCH_DIR})

   echo "${SUMMARY}"
   echo "Report file generated: ${COVERAGE_REPORT}"

   # generate badges
   LINE_LABEL=`echo ${SUMMARY} | awk 'NR==1{print $1}'`
   LINE_VALUE=`echo ${SUMMARY} | awk 'NR==1{print $2}'`
   LINE_VALUE_NUM=`echo ${LINE_VALUE} | sed 's/%//g'`
   LINE_VALUE_NUM=$(echo "($LINE_VALUE_NUM*100)/1"|bc)

   BRANCH_LABEL=`echo ${SUMMARY} | awk 'NR==1{print $7}'`
   BRANCH_VALUE=`echo ${SUMMARY} | awk 'NR==1{print $8}'`
   BRANCH_VALUE_NUM=`echo ${BRANCH_VALUE} | sed 's/%//g'`
   BRANCH_VALUE_NUM=$(echo "($BRANCH_VALUE_NUM*100)/1"|bc)

   echo ${LINE_LABEL} ${LINE_VALUE} ${LINE_VALUE_NUM}
   echo ${BRANCH_LABEL} ${BRANCH_VALUE} ${BRANCH_VALUE_NUM}

   COLOR=success
   if [[ $LINE_VALUE_NUM -lt 5500 ]]; then
      COLOR=critical
   elif [[ $BRANCH_VALUE_NUM -lt 4000 ]]; then
      COLOR=critical
   fi

   # badge json
   # write out json endpoint for each value
   echo "{ \
   \"schemaVersion\": 1,
   \"label\": \"Coverage\",
   \"message\": \"$LINE_LABEL $LINE_VALUE $BRANCH_LABEL $BRANCH_VALUE\",
   \"color\": \"$COLOR\"
   }" > ${OUTPUT_DIR}/badge.json

}

main
