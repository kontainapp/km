#!/bin/bash
# Nokia start script modified for the Kontain build environment.

KM_BASE=$(git rev-parse --show-toplevel)
#set -o errexit


TEST_CASE=$1


TEST_DATETIME=`date +%Y%m%d%H%M`

BASE_DIR=${KM_BASE}/demo/nokia/kafka-test-tool

RESULT_DIR=${KM_BASE}/build/demo/nokia/results

TEST_INSTANCE_DIR=$RESULT_DIR/$TEST_DATETIME

echo $TEST_INSTANCE_DIR

mkdir -p $TEST_INSTANCE_DIR

readonly LOG_FILE=$TEST_INSTANCE_DIR/$TEST_DATETIME-output.log
touch $LOG_FILE
exec 1>$LOG_FILE
exec 2>&1




$BASE_DIR/bin/sar.sh $TEST_INSTANCE_DIR/$TEST_DATETIME-sar.txt &

echo "Running Test Case $TEST_CASE"
$BASE_DIR/bin/$TEST_CASE.sh

echo "Killing sar"
kill -9 `ps -ef |grep vagrant|grep -v grep|grep sar |awk '{ print  $2 }'`
