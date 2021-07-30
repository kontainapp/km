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
# Test to verify that the gdb client can asynchronously attach and detach, attach and detach
# repeatedly.
#
#  shell_1> ../build/km/km ./gdb_lots_of_threads_test.kmd
#  shell_2> gdb -q -nx --ex="target remote :2159"  \
#                      --ex="set stop_running=[0|1]" \
#                      --ex="source cmd_for_attach_test.gdb"  --ex q
#
set pagination off
print stop_running

# just do something to prove we are attached to the target
info threads
thread 4
backtrace

# detach from the target
set confirm off
quit

# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
