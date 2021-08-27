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

usage() {
   cat <<EOF
Run short or long node tests. Usually called from Makefile or Docker Entry with proper params
Usage:
   short tests: test-run.sh test KM_BIN PAYLOAD_KM TEST_KM
   long  tests: test-run.sh test-all NODETOP BUILD
EOF
   exit 1
}

echo "Checking for /dev/k.m"
ls -l /dev/k.m
case "$1" in
  test)
   KM_BIN=$2
   PAYLOAD_KM=$3
   TEST_KM=$4

	${KM_BIN} -V ${PAYLOAD_KM} ./scripts/hello.js
	echo noop.js - expecting exit with code 22:
	${KM_BIN} ${PAYLOAD_KM} ./scripts/noop.js || [ $? -eq 22 ]
	${KM_BIN} ${PAYLOAD_KM} ./scripts/micro-srv.js & sleep 1 ; curl localhost:8080
	curl -X POST localhost:8080 || echo Forcing srv to exit and ignoring curl 'empty reply'
	${KM_BIN} ${TEST_KM} --gtest_filter="*"
  ;;

  test-all)
   NODETOP=$2
   BUILD=$3
	cd ${NODETOP}
   /bin/python2.7 tools/test.py -J --mode=`echo -n ${BUILD} | tr '[A-Z]' '[a-z]'` --skip-tests=`cat ../skip_* | tr '\n' ','` default addons js-native-api node-api
   echo "Tests are Successful"
  ;;

  *)
  usage
  ;;
esac

