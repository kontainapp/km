#!/bin/bash
#
# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
set -e ; [ "$TRACE" ] && set -x

attempt_counter=0
max_attempts=250

START=$(date +%s%N)

/opt/kontain/bin/km /opt/kontain/java/bin/java.kmd -jar /app.jar &
#/opt/kontain/bin/km --resume kmsnap &
PID=$!

DFINISH=$(date +%s%N)

until $(curl --output /dev/null --silent --fail http://localhost:8080/greeting); do
    if [ ${attempt_counter} -eq ${max_attempts} ];then
      echo "Max attempts reached"
      kill $PID
      exit 1
    fi

    attempt_counter=$(($attempt_counter+1))
    sleep 0.01
done
FINISH=$(date +%s%N)
echo $((${DFINISH} - ${START})), $((${FINISH} - ${DFINISH})), $((${FINISH} - ${START}))

kill $PID
