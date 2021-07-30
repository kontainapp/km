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

[ "$TRACE" ] && set -x

KM_TOP=$(git rev-parse --show-toplevel)

${KM_TOP}/build/km/km ./server.km > /dev/null 2>&1 &

sleep 0.3

for i in $(seq 3) ; do
   curl --silent http://127.0.0.1:8080 | grep -q 'Hello World!'
   if [ $? != 0 ]; then
      kill -KILL %%
      exit
   fi
done

kill -KILL %%

