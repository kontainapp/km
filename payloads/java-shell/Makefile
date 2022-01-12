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

COMPONENT := jdk-shell-11.0.8

# name to be added to label(s)
PAYLOAD_NAME := ${COMPONENT}
PAYLOAD_FILES :=

CONTAINER_TEST_CMD := true

include ${TOP}/make/images.mk

test test-krun test-all:
	@echo Skipping $@

clean clobber:
	@true

RUNENV_VALIDATE_CMD := ("sh" "-c" "java -version")
RUNENV_VALIDATE_EXPECTED := openjdk version

RUNENV_TAG := java-shell-11 jdk-shell-11 ${COMPONENT}
