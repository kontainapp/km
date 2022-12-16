#! /usr/bin/env bats
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
set -e

load "${TESTS_BASE}/bats-support/load.bash" # see manual in bats-support/README.md
load "${TESTS_BASE}/bats-assert/load.bash"  # see manual in bats-assert/README.mnd

@test "Java version" {
  run ${KM_BIN} --putenv=LD_LIBRARY_PATH=${JAVA_LD_PATH} \
		${JAVA_DIR}/bin/java.kmd -version
  assert_success
  assert_output --partial 'OpenJDK Runtime Environment'
}

@test "Java Hello World" {
  run ${KM_BIN} --putenv=LD_LIBRARY_PATH=${JAVA_LD_PATH} \
		${JAVA_DIR}/bin/java.kmd -cp scripts Hello
  assert_success
  assert_output --partial 'Hello, World!'
}

@test "Java snapshot" {
  SNAP_FILE=/tmp/kmsnap_java.$$
  rm -f ${SNAP_FILE}

  run ${KM_BIN} --snapshot=${SNAP_FILE} \
    --putenv=LD_LIBRARY_PATH=${JAVA_LD_PATH}:${BLDDIR}/lib \
		${JAVA_DIR}/bin/java.kmd -cp ${BLDDIR}/mods/app.kontain \
    app/kontain/snapshots/Snapshot
  assert_success
  assert_output --partial 'Kontain API'
  assert [ -f ${SNAP_FILE} ]

  run ${KM_BIN} ${SNAP_FILE}
  assert_success
  assert_output --partial 'past snapshot'

  rm -f ${SNAP_FILE}
}

@test "Java SpringBoot snapshot" {
  SNAP_FILE=kmsnap
  MGMTPIPE=/tmp/xxx.$$
  rm -f ${SNAP_FILE}

  ${KM_BIN} -Vsnapshot --mgtpipe=${MGMTPIPE} --snapshot=${SNAP_FILE} \
      --putenv=LD_LIBRARY_PATH=${JAVA_LD_PATH}:${BLDDIR}/lib \
		${JAVA_DIR}/bin/java.kmd -XX:-UseCompressedOops -jar sb.jar &
  pid=$!
  # Starting springboot in a github runner can take quite a while on a bad day.
  tries=120
  run curl -4 localhost:8080/greeting --retry-connrefused  --retry $tries --retry-delay 1
  assert_success
  assert_output --regexp '"id":1.*Hello, World!'

  ${KM_CLI_BIN} -s ${MGMTPIPE} -t
  wait $pid
  assert_success
  assert [ -f ${SNAP_FILE} ]

  ${KM_BIN} -Vsnapshot ${SNAP_FILE} &
  pid=$!
  run curl -4 -s localhost:8080/greeting --retry-connrefused  --retry $tries --retry-delay 1
  assert_success
  assert_output --regexp '"id":2.*Hello, World!'
  run curl -4 -s localhost:8080/greeting --retry-connrefused  --retry $tries --retry-delay 1
  assert_success
  assert_output --regexp '"id":3.*Hello, World!'
  kill $pid
  wait $pid || true

  SNAP_LISTEN_PORT=$(cat ${SNAP_FILE}.conf) ${KM_BIN} -Vsnapshot ${SNAP_FILE} &
  pid=$!
  run curl -4 -s -H "User-Agent: kube-probe" localhost:8080/greeting --retry-connrefused  --retry $tries --retry-delay 1
  assert_success
  run curl -4 -s -H "User-Agent: kube-probe" localhost:8080/greeting --retry-connrefused  --retry $tries --retry-delay 1
  assert_success
  run curl -4 -s localhost:8080/greeting --retry-connrefused  --retry $tries --retry-delay 1
  assert_success
  assert_output --regexp '"id":2.*Hello, World!'
  run curl -4 -s localhost:8080/greeting --retry-connrefused  --retry $tries --retry-delay 1
  assert_success
  assert_output --regexp '"id":3.*Hello, World!'
  kill $pid
  rm -f ${SNAP_FILE} ${SNAP_FILE}.conf
}
