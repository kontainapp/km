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
# Helper gdb script to verify gdb can read from and write to protected target memory.
# In the tests directory:
# to test km gdb support from ./tests:
#  shell_1> ../build/km/km -g 2159 ./gdb_protected_mem_test.km
#  shell_2> gdb -q -nx --ex="target remote :2159"  --ex="source cmd_for_protected_mem_test.gdb" --ex=c \
#             --ex=q gdb_protected_mem_test.km

# Display a tag and the contents of a memory location on one line
# arg0 - tag
# arg1 - memory location
define tagged_mem_display
  printf $arg0
  printf " "
  x /gx $arg1
end

br main
continue

br gdb_protected_mem_test.c:40
continue

# first word
p *(unsigned long*)memchunk
set *(unsigned long*)memchunk=0x1111111111111111
tagged_mem_display "first word " memchunk

# spanning 2 pages
p *(unsigned long*)(memchunk+4092)
set *(unsigned long*)(memchunk+4092)=0xff0000ffff0000ff
tagged_mem_display "spanning pages " memchunk+4092

# last word
p *(unsigned long*)(memchunk+8192-8)
set *(unsigned long*)(memchunk+8192-8)=0xeeeeeeeeeeeeeeee
tagged_mem_display "last word " memchunk+8192-8

continue

quit
