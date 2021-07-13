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
# Very basic sanity checks for python in KM
import os
import _ctypes

# If we are here, we run ok-ish
if os.uname().release.find("kontain") == -1:
   raise BaseException(f"We don't seem to be running under Kontain Monitor.\n{os.uname()}")

# make sure embedded modules (at least _ctypes)
if '_objects' not in dir(_ctypes.Union):
   raise BaseException(f"Error in built-in load: _objects not found in _ctypes.Union.\n{dir(_ctypes.Union)}")

print("Hello from python in Kontain VM: sanity check passed")
