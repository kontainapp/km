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
# Test for GDB stub handing of guest signals.
#
# to test km gdb signal support from ./tests:
#  shell_1> ../build/km/km -g3333 ./stray_test.km signal
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_signal_test.gdb"  --ex c --ex q stray_test.km
#

set print pretty
set pagination off

p/s "** should stop for SIGUSR1:"
continue

p/s "** should stop for SIGABRT:"
continue

# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
