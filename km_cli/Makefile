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


TOP := $(shell git rev-parse --show-toplevel)

EXEC := km_cli
SOURCES := km_client.c
VERSION_SRC := km_client.c # it has branch/version info, so rebuild it if git info changes
LOCAL_LDOPTS := -static
LOCAL_COPTS := -I${TOP}/lib/libkontain

include ${TOP}/make/actions.mk
