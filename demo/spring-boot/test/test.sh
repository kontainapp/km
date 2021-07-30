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
# This is a minimal script used to get time to active readings.
#
set -e
[ "$TRACE" ] && set -x

CONTAINER=KM_SpringBoot_Demo

until $(curl --output /tmp/out.json --silent --fail http://localhost:8080/greeting); do
   sleep 0.001
done

end=$(date +%s%N)
start=$(date +%s%N -d $(ls --full-time start_time | awk -e '{print $7}'))
dur=$(expr $end - $start)
echo "Response time $(expr $dur / 1000000000).$(printf "%.03d" $(expr $dur % 1000000000 / 1000000)) secs"
jq . /tmp/out.json 
echo ""
