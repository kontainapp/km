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
# Basic build for kontain payloads
#

# scan all these and 'make' stuff there
SUBDIRS := busybox dynamic-base dynamic-base-large demo-dweb python node java dynamic-python

demo-dweb python node dynamic-base dynamic-base-large: busybox

java: dynamic-base

dynamic-python: dynamic-base-large

# Filter out 'make test' requests' to assure that 'make test' only builds/tests KM.
# If payload needs to be tested, use 'make test-all'
# Also, just 'make' will not cause building payloads , so it is reserved for building KM only.
# Use 'make all' or 'make build' to request payloads build

default test coverage:
	@echo "Info: ignoring target '$@' in payloads"

# use this target to force payloads build
link build: $(SUBDIRS)

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/actions.mk
