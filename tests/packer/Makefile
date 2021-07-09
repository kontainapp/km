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


# A basic Makefile to make KKM AWS test slightly more uniform
# (this is currently used only for KKM testing on AWS)
#
# to run locally, pass IMAGE_VERSION for an existing test pass, e.g.
# make IMAGE_VERSION=ci-146

TOP := $(shell git rev-parse --show-toplevel)

LOG ?= 0
PACKER_BUILD = PACKER_LOG=${LOG} time packer build

test: .check_packer .check_image_version ## Test KKM build on AWS using Hashicorp Packer
	${PACKER_BUILD} \
		--var src_branch=${SRC_BRANCH} \
		--var image_version=${IMAGE_VERSION} \
		km-test.pkr.hcl

include ${TOP}/make/locations.mk
