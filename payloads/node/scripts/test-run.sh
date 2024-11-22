#!/usr/bin/bash -ex
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
#
# wrapper/entrypoint for running tests
#

export OPENSSL_CONF=/etc/ssl

usage() {
   cat <<EOF
Run short or long node tests. Usually called from Makefile or Docker Entry with proper params
Usage:
   test-run.sh < test | test-all | test-short | test-long > KM_BIN KM_CLI_BIN PAYLOAD_KM TEST_KM NODETOP BUILD
EOF
   exit 1
}

function cleanup() {
   rm -f ${TMP_FILE}
}

check_crypto() {
   msg="Unsupported crypto setting for this version of node. Use 'sudo update-crypto-policies --set LEGACY'"
   source /etc/os-release
   [[ $PLATFORM_ID == platform:f35 ]] && [[ $(update-crypto-policies --show) == DEFAULT ]] && echo $msg && exit 1
   return 0
}
check_crypto

KM_BIN=$2
KM_CLI_BIN=$3
PAYLOAD_KM=$4
TEST_KM=$5

if [[ "$1" == "test-short" || "$1" == "test" || "$1" == "test-all" ]]; then
   MGMTPIPE=/tmp/mgmtpipe.$$

   ${KM_BIN} ${PAYLOAD_KM} ./scripts/hello.js
   echo noop.js - expecting exit with code 22:
   ${KM_BIN} ${PAYLOAD_KM} ./scripts/noop.js || [ $? -eq 22 ]
   (cd node; ${KM_BIN} ${TEST_KM} --gtest_filter="*")

   rm -rf ${MGMTPIPE}
   ${KM_BIN} --mgtpipe=${MGMTPIPE} ${PAYLOAD_KM} ./scripts/micro-srv.js &
   pid=$!
   tries=5
   curl -4 -s localhost:8080 --retry-connrefused  --retry $tries --retry-delay 1
   ${KM_CLI_BIN} -s ${MGMTPIPE} -t
   wait $pid

   ${KM_BIN} kmsnap &
   pid=$!
   curl -4 -s localhost:8080 --retry-connrefused  --retry $tries --retry-delay 1
   curl localhost:8080
   curl -X POST localhost:8080 || echo Forcing srv to exit and ignoring curl 'empty reply'
   wait $pid

   SNAP_LISTEN_PORT=$(cat kmsnap.conf) ${KM_BIN} kmsnap &
   pid=$!
   curl -4 -s -H "User-Agent: kube-probe" localhost:8080 --retry-connrefused  --retry $tries --retry-delay 1
   curl -4 -s -H "User-Agent: kube-probe" localhost:8080 --retry-connrefused  --retry $tries --retry-delay 1
   curl localhost:8080
   curl localhost:8080
   curl -X POST localhost:8080 || echo Forcing srv to exit and ignoring curl 'empty reply'
   wait $pid
fi

if [[ "$1" == "test-long" || "$1" == "test-all" ]]; then
   NODETOP=$6
   BUILD=$7
   trap cleanup EXIT
   TMP_FILE=$(mktemp /tmp/node.XXXXXXXXXX)
   echo -e "#!/bin/bash\nexport PYTHONWARNINGS=\"ignore::DeprecationWarning\"\n${KM_BIN} $(realpath ${NODETOP}/out/${BUILD}/node.km) \$*\n" > ${TMP_FILE} && chmod +x ${TMP_FILE}
   cd ${NODETOP}
   python tools/test.py --shell=${TMP_FILE} --timeout=120 -J --mode=`echo -n ${BUILD} | tr '[A-Z]' '[a-z]'` --skip-tests=`cat ../skip_* ../${PLATFORM_ID}_skip | tr -s '\n ' ','` default addons js-native-api node-api
   echo "Tests are Successful"
fi

if [[ "$1" != "test-short" && "$1" != "test-long" && "$1" != "test" && "$1" != "test-all" ]]; then
  usage
fi

