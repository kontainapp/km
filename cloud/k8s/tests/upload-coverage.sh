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
# Upload the coverage report to a github repo.

set -e ; [ "$TRACE" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CURRENT=$(readlink -m $(dirname $0))
readonly REPORT_PATH=$1
readonly REPORT_NAME=$2
readonly GITHUB_TOKEN=$3

readonly TIME=$(date -u)

readonly TOP=$(git rev-parse --show-toplevel)
readonly REPORT_REPO_WORKDIR=${TOP}/build/km-coverage-report
readonly TEMP_REPORT=${REPORT_REPO_WORKDIR}/.report
readonly REPORT_DIR=${REPORT_REPO_WORKDIR}/report

# On azure pipeline, we need to use HTTPS version of url to clone git repo. On
# local machine, we use SSH version.
if [[ ! -z ${PIPELINE_WORKSPACE} ]]; then
    # We use github personal access token to authenticate on azure pipeline.
    # Make sure it's in the variable.
    if [[ -z ${GITHUB_TOKEN} ]]; then
        echo "No GITHUB_TOKEN specified on azure pipeline"
        exit 1
    fi
fi

# Without github token, we will try using ssh authentication.
if [[ -z ${GITHUB_TOKEN} ]]; then
    readonly REPORT_REPO_URL=git@github.com:kontainapp/km-coverage-report.git
else
    readonly REPORT_REPO_URL=https://${GITHUB_TOKEN}@github.com/kontainapp/km-coverage-report.git
fi


function usage() {
    cat <<- EOF
usage: $PROGNAME <REPORT_PATH> <REPORT_NAME> <GITHUB_TOKEN>

Helper script to update the coverage report.
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
    if [[ -z $REPORT_PATH ]]; then usage; fi
    if [[ -z $REPORT_NAME ]]; then usage; fi

    # Process the repo. If the report repo don't exist, we need to clone it. If
    # the repo exist, we need to reset any local changes to match the remote.
    if [[ -d ${REPORT_REPO_WORKDIR} ]]; then
        cd ${REPORT_REPO_WORKDIR}
        git checkout master
        git fetch
        # Use this to clean any residule changes on local. For a new upload, we
        # need to be in sync with the remote master.
        git reset --hard origin/master
    else
        git clone ${REPORT_REPO_URL} ${REPORT_REPO_WORKDIR}
        cd ${REPORT_REPO_WORKDIR}
    fi

    # On azure pipeline, git needs to be configure.
    if [[ ! -z ${PIPELINE_WORKSPACE} ]]; then
        git config user.email "azure-nightly-pipeline@kontain.app"
        git config user.name "Azure Nightly Pipeline"
    fi

    # Git will not automatically fetch all the tags, so we force it here.
    git fetch --tags --force
    check_tag $REPORT_NAME

    # Replace the existing report with the latest reports. Commit the new
    # changes and tag the commit with a name.
    mkdir -p ${TEMP_REPORT}
    cp ${REPORT_PATH}/*.html ${TEMP_REPORT}/
    rm -rf ${REPORT_DIR}
    mv ${TEMP_REPORT} ${REPORT_DIR}
    git add ${REPORT_DIR}
    git commit -a -m "KM Coverage Report: ${TIME} ${REPORT_NAME}"
    if [ ! -z "$(git status --porcelain)" ]; then
        echo "git status is not clean"
        git status
        exit 1
    fi
    git tag $REPORT_NAME
    git push && git push origin tag $REPORT_NAME
}

main
