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
# Making  cutsom endpoints for custom coverage badges
set -e ; [ "$TRACE" ] && set -x

set -x

readonly OUTPUT_DIR=$1
SUMMARY_FILE=${OUTPUT_DIR}/summary.txt

LINE_LABEL=`awk 'NR==1{print $1}' ${SUMMARY_FILE}`
LINE_VALUE=`awk 'NR==1{print $2}' ${SUMMARY_FILE}`

BRANCH_LABEL=`awk 'NR==2{print $1}' ${SUMMARY_FILE}`
BRANCH_VALUE=`awk 'NR==2{print $2}' ${SUMMARY_FILE}`

# figure out color
[[ $LINE_VALUE -lt 55 ]] && LINE_COLOR=critical || LINE_COLOR=success
[[ $BRANCH_VALUE -lt 40 ]] && BRANCH_COLOR=critical || BRANCH_COLOR=success

# write out json endpoint for each value
echo "{
  "schemaVersion": 1,
  "label": "$LINE_LABEL",
  "message": "$LINE_VALUE",
  "color": "$LINE_COLOR"
}" > ${OUTPUT_DIR}/gcov_lines.json

echo "{
  "schemaVersion": 1,
  "label": "$BRANCH_LABEL",
  "message": "$BRANCH_VALUE",
  "color": "$BRANCH_COLOR"
}" > ${OUTPUT_DIR}/gcov_branches.json
