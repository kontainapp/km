#!/bin/bash
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
set -e ; [ "$TRACE" ] && set -x

CONTAINER="kontainapp/spring-boot-demo:latest"

attempt_counter=0
max_attempts=250

START=$(date +%s%N)

docker run --runtime=krun -it --name TESTDEMO -d -p8080:8080 ${CONTAINER}
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
