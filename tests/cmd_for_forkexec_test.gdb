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
# We should be attaching after the fork returns in the child process.
# This happens because weve set the KM_GDB_FORK_CHILD_WAIT environment variable to have
# km's gdb stub pause after fork in the child.
# Print the program path so we can verify it is what we expect.
backtrace
frame 3
printf "post fork prog: %s\n", argv[0]
catch exec
cont
# At this point we should be at _start of what the child process exec'ed to.
# Run up to main() and then print out the path of what is currently running
# so can verify the exec happened.
br main
cont
backtrace
printf "post exec prog: %s\n", argv[0]
cont
quit
