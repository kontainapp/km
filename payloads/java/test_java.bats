#! /usr/bin/env bats
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
    --putenv=LD_LIBRARY_PATH=${JAVA_LD_PATH}:$(pwd)/app/kontain/snapshots \
		${JAVA_DIR}/bin/java.kmd app/kontain/snapshots/Snapshot
  assert_success
  assert_output --partial 'Kontain API'
  assert [ -f ${SNAP_FILE} ]

  run ${KM_BIN} --resume ${SNAP_FILE}
  assert_success
  assert_output --partial 'past snapshot'

  rm -f ${SNAP_FILE}
}