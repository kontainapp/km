#
# Copyright 2022 Kontain Inc
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

LOG ?= 0
PACKER_BUILD = PACKER_LOG=${LOG} time packer build

L0_VMIMAGE_NAME ?= L0BaseImage
L0-image: ## build the very base image
	${PACKER_BUILD} -force -var image_name=${L0_VMIMAGE_NAME} L0-image.pkr.hcl

include $(TOP)/make/locations.mk

