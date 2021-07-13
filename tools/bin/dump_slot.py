#!/usr/bin/env python3
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
#
# Given a guest virtual address, print the slot numbers
# for the guest page tables.
import sys

val = int(sys.argv[1], 0)
print("2mb slot {}".format((val & 0x3fffffff) >> 21))
print("1gb slot {}".format((val & 0x7fffffffff) >> 30))
