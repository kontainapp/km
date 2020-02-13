#!/bin/bash

KM_BIN=$1
REDIS_KM=$2

# Launch Redis using KM, then ping the port. If successful, the output should
# be +PONG. Then clean up first before we check if the output matches our
# expectation.
${KM_BIN} ${REDIS_KM} > /dev/null & sleep 1
PID=$!
OUT=$(echo PING | nc localhost 6379 | grep '+PONG')
kill -9 ${PID}

[ -n "$OUT" ]