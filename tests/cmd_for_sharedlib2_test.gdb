#
# Copyright 2021 Kontain Inc.
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
# Test to verify that symbols brought in by dlopen() are available.
#
#  shell_1> ../build/km/km -g ./gdb_sharedlib2_test.kmd
#  shell_2> gdb -q -nx --ex="target remote :2159"  \
#                      --ex="source cmd_for_sharedlib2_test.gdb"  --ex c --ex q
#

set pagination off

# at this point the shared library is loaded so place a breakpoint using a symbol
# from the loaded library
br got_symbol_breakpoint
commands
  br do_function
  continue
end

br hit_breakpoint
commands
  info sharedlibrary
  p symvalue
  disassem symvalue
  continue
end

continue

# we've hit the breakpoint placed on do_function
printf "Hit the breakpoint at do_function\n"
backtrace
print $rip
continue


# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
