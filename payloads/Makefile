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
SUBDIRS := busybox dynamic-base dynamic-base-scratch dynamic-base-large demo-dweb python node java java-shell dynamic-python

TOP := $(shell git rev-parse --show-toplevel)

BLDTOP := ${TOP}/build
KM_OPT := ${BLDTOP}/opt/kontain
KM_OPT_BIN := ${KM_OPT}/bin

include ${TOP}/make/actions.mk

dynamic-base dynamic-base-large dynamic-base-scratch: test-rebuild

demo-dweb python node dynamic-base dynamic-base-large: busybox

java-shell: dynamic-base java

java: dynamic-base-scratch

dynamic-python: dynamic-base-large python

test-rebuild:
ifneq (, $(or $(filter runenv-image,$(MAKECMDGOALS)), $(filter demo-runenv-image,$(MAKECMDGOALS))))
	rm -f $(TOP)/tests/hello_test.kmd
	PATH=${KM_OPT_BIN}:${PATH} make -C $(TOP)/tests RPATH=/opt/kontain hello_test.kmd
endif

# Also, just 'make' will not cause building payloads , so it is reserved for building KM only.
# Use 'make all' or 'make build' to request payloads build

default coverage:
	@echo "Info: ignoring target '$@' in payloads"

# use this target to force payloads build
link build clobber: $(SUBDIRS)
