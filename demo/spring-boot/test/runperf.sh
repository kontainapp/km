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

CONTAINER="kontainapp/spring-boot-demo:latest"

attempt_counter=0
max_attempts=250

START=$(date +%s%N)

docker run -it --name TESTDEMO -d --device=/dev/kvm \
  -v /opt/kontain/bin/km:/opt/kontain/bin/km:z \
  -v /opt/kontain/runtime/libc.so:/opt/kontain/runtime/libc.so:z \
  -v /opt/kontain/bin/km_cli:/opt/kontain/bin/km_cli:z \
  -v ${WORKSPACE}/km/payloads/java/scripts:/scripts:z \
  -p8080:8080 ${CONTAINER}
#/opt/kontain/bin/km /opt/kontain/java/bin/java.kmd -jar /app.jar &
#/opt/kontain/bin/km kmsnap &

DFINISH=$(date +%s%N)

until $(curl --output /dev/null --silent --fail http://localhost:8080/greeting); do
    if [ ${attempt_counter} -eq ${max_attempts} ];then
      echo "Max attempts reached"
      docker stop TESTDEMO
      docker rm TESTDEMO
      exit 1
    fi

    attempt_counter=$(($attempt_counter+1))
    sleep 0.01
done
FINISH=$(date +%s%N)
echo $((${DFINISH} - ${START})), $((${FINISH} - ${DFINISH})), $((${FINISH} - ${START}))

docker stop TESTDEMO
docker rm TESTDEMO
kill %1