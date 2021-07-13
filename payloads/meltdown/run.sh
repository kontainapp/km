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

TMPNAME="/tmp/xxx_"$$

trap "{ sudo pkill secret; rm -f $TMPNAME; exit; }" SIGINT

sudo taskset 0x4 ./meltdown/secret | tee $TMPNAME &
sleep 1
taskset 0x1 ./meltdown/physical_reader `awk '/Physical address of secret:/{print $6}' $TMPNAME` 0xffff888000000000
