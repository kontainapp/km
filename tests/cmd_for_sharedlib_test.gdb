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
# Test to see if the "info auxv" and "info sharedlibrary" commands are
# producing output that is sensible.
# The "info sharedlibrary" test only makes sense for the .kmd version
# of the test program
#
# to test gdb attaching at entry to the dynamic linker
#  shell_1> ../build/km/km -G ./stray_test.kmd stray
#  shell_2> gdb -q -nx --ex="target remote :2159"  \
#                      --ex="source cmd_for_sharedlib_test.gdb"  --ex c --ex q
# We also us -g with this test script to test for entry at _start.
#

set pagination off

info auxv

info sharedlibrary

# run til segfault
continue

#
continue


# For some reason 'cont' generates 'error in command file'
# from gdb client for KM remote debug (but works with gdbserver)
# so finishing the script.
# Please do '--ex c --ex q' in gdb command
