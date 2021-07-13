#!hello_test
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

set -x
# Only the first line is looked at when passed to KM as a payload file
# The rest are ignored for now, but in the future we may add here config info
# or some build-in fun,  e.g.
snapshot_interval 15sec
snapshot_dir /mySnapshots
support_shell false
verbose (mmap|mem)
gdb_listen true

