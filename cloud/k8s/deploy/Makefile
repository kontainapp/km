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
# The image will be used to install kontain artifacts on each node.

TOP := $(shell git rev-parse --show-toplevel)

# Undefine CLOUD for make/locactions.mk (included my make/images.mk)
#  doesn't try to pick up Azure defs.
CLOUD :=

# Definitions used within make/images.mk for runenv
COMPONENT := k8s-deploy
RUNENV_TAG := ${COMPONENT}
REGISTRY := docker.io/kontainapp

# runenv-image configs
export define runenv_prep
        mkdir -p ${BLDDIR}/bin && cp ${KM_BIN} $$_
	cp ${BLDTOP}/container-runtime/krun ${BLDDIR}/bin
        mkdir -p ${BLDDIR}/scripts && cp k8s-deploy.sh $$_
endef

.DEFAULT_GOAL := runenv-image

include ${TOP}/make/images.mk
