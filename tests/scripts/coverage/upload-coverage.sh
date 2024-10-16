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
# Upload the coverage report to a github repo.

set -e ; [ "$TRACE" ] && set -x

# usage upload-coverage.sh <report-directory> <github-token>
readonly PROGNAME=$(basename ${0})
readonly REPORT_PATH=${1}
readonly TAG=${2}
readonly GITHUB_TOKEN=${3}

readonly TIME=$(date -u)

readonly CURRENT_USER=$(id -u -n)
readonly USER_HOME=$(eval echo ~$CURRENT_USER)
readonly REPORT_REPO_WORKDIR=${USER_HOME}/km-coverage-report

function usage() {
    cat <<- EOF
usage: $PROGNAME <REPORTS_DIR> <IMAGE_VERSION> <GITHUB_TOKEN>

Upload generated HTML report to github reporitory
All report .html files must be located in REPORTS_DIR
IMAGE_VERSION - to be used as tag
GITHUB_TOKEN - github token or user name

EOF
    exit 1
}

function check_tag {
    local tag=$1

    if [[ -z $tag ]]; then usage; fi

    # Need to error out if tag already exists.
    if [[ $(git tag -l $tag) != "" ]]; then
        echo "Version $tag already exist"
        exit 1
    fi
}

function main {

   if [[ -z ${GITHUB_TOKEN} ]]; then
      REPORT_REPO_URL=git@github.com:kontainapp/km-coverage-report.git
   else
      REPORT_REPO_URL=https://${GITHUB_TOKEN}@github.com/kontainapp/km-coverage-report.git
   fi


   # clone report repository
   git clone ${REPORT_REPO_URL} ${REPORT_REPO_WORKDIR}
   cd ${REPORT_REPO_WORKDIR}
   # configure identity
   if [[ ! -z ${GITHUB_TOKEN} ]]; then
      git config user.email "coverage-pipeline@kontain.app"
      git config user.name "Coverage Pipeline"
   fi

   # Git will not automatically fetch all the tags, so we force it here.
   git fetch --tags --force
   check_tag ${TAG}

   # remove all checkpout files
   rm -f report/*.html
   # copy new report files here
   cp ${REPORT_PATH}/*.html report/
   # copy badge.json
   cp ${REPORT_PATH}/badge.json report/
   # stage all changes, including new files
   git add --all
   # commit changes
   git commit -m "KM Coverage Report: ${TIME} ${TAG}"
   # add tag
   git tag --force ${TAG}
   git push https://${GITHUB_TOKEN}@github.com/kontainapp/km-coverage-report.git
   git push --tags --force https://${GITHUB_TOKEN}@github.com/kontainapp/km-coverage-report.git

   # # delete reports directory
   rm -rf ${REPORT_REPO_WORKDIR}
}

main
