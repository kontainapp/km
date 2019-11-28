#!/usr/bin/bash -ex
#
# wrapper/entrypoint for running tests
#

usage() {
   cat <<EOF
Run python tests. Usually called from Makefile or Docker Entry with proper params
Usage: test-run.sh KM_BIN PAYLOAD_KM
EOF
   exit 1
}

KM_BIN=$1
PAYLOAD_KM=$2

${KM_BIN} ${PAYLOAD_KM} ./test_unittest.py
${KM_BIN} ${PAYLOAD_KM} ./cpython/Lib/unittest/test/
