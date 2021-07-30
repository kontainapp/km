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
br main
continue

# We should be at the point where we call next_over_this_function()
# So, we perform a next command and later in the bats script ensure we "next"ed over the function
next
next

# We should be at the point where we call step_into_this_function()
# So, we perform s step command and later in the bats script ensure we "step"ed into the function.
step
step

# All done
continue
