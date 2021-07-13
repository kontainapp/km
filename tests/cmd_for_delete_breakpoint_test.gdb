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
# Simple test for deleting pending breakpoints
#
# in the tests directory:
# to test km gdb support from ./tests:
#  shell_1> ../build/km/km -Vgdb -g 2159 ./gdb_delete_breakpoint_test.km
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_delete_breakpoint_test.gdb" --ex=c \
#             --ex=q gdb_delete_breakpoint_test.km
#

br disable_breakpoint
commands
  disable breakpoint 3
  continue
end

br enable_breakpoint
commands
  enable breakpoint 3
  continue
end

# This is breakpoint 3
br hit_breakpoint
commands
  print "Hit breakpoint"
  continue
end

set pagination off

define hook-quit
  set confirm off
end

cont
