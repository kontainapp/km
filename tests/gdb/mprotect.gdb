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
# prints mmaps status on dummy_hcall.
# mprotect_test.c is expected to call dummy_Hcall after each test
# so we can automate maps validation

source ../km/gdb/list.gdb

break km_hcalls.c:dummy_hcall
command
print "Busy list"
print_tailq &machine.mmaps.busy
print "Free list"
print_tailq &machine.mmaps.free
end
