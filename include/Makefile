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

TOP := $(shell git rev-parse --show-toplevel)

include ${TOP}/make/actions.mk

all: km_hcalls.h
	[ -d ${KM_OPT_INC} ] || mkdir ${KM_OPT_INC}
	cp km_hcalls.h ${KM_OPT_INC}

