#!/bin/bash
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
# Scripts to help install or uninstall kontaind.

[ "${TRACE}" ] && set -x

readonly PROGNAME=$(basename $0)
readonly CURRENT_DIR=$(readlink -m $(dirname $0))

readonly OP=$1
readonly INSTALLER_IMAGE=$2
readonly DEVICE_PLUGIN_IMAGE=$3

readonly KONTAIND_DEPLOY_DIR=${CURRENT_DIR}/.kontaind_deploy
readonly KONTAIND_DEPLOY_TEMPLATE_NAME=kontaind.yaml

function usage() {
    cat <<- EOF
usage: $PROGNAME <OP> <INSTALLER_IMAGE> <DEVICE_PLUGIN_IAGE> 

Install or uninstall kontaind.

OP:
    install                 Install kontaind.
    uninstall               Uninstall kontaind.
EOF
}

function install() {
    rm -rf ${KONTAIND_DEPLOY_DIR} && mkdir -p ${KONTAIND_DEPLOY_DIR}
    m4 \
        -D INSTALLER_IMAGE=${INSTALLER_IMAGE} \
        -D DEVICE_PLUGIN_IMAGE=${DEVICE_PLUGIN_IMAGE} \
        ${CURRENT_DIR}/${KONTAIND_DEPLOY_TEMPLATE_NAME} > \
        ${KONTAIND_DEPLOY_DIR}/${KONTAIND_DEPLOY_TEMPLATE_NAME}

    kubectl create -f ${KONTAIND_DEPLOY_DIR}/${KONTAIND_DEPLOY_TEMPLATE_NAME}
}

function uninstall() {
    kubectl delete -f ${KONTAIND_DEPLOY_DIR}/${KONTAIND_DEPLOY_TEMPLATE_NAME}
}

function main() {
    case $OP in
    "install")
        install
        ;;
    "uninstall")
        uninstall
        ;;
    *)
        usage
        exit 1
        ;;
    esac
}

main