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

COMPONENT = kontain-installer
RUNENV_TAG := ${COMPONENT}

# runenv-image configs
export define runenv_prep
	mkdir -p ${BLDDIR}/bin && cp ${KM_BIN} $$_
	cp installer.sh ${RUNENV_PATH}/installer.sh
endef

.DEFAULT_GOAL := runenv-image

.PHONY: deploy delete test clean

# TODO: set the right image and version once we have over own device plugin
DEVICE_PLUGIN_KVM_IMG ?= quay.io/kubevirt/device-plugin-kvm:latest

deploy: ## Deploy Kontaind and cored to target Kubernetes cluster
	./deploy.sh deploy ${RUNENV_IMG_REG}:${IMAGE_VERSION} ${DEVICE_PLUGIN_KVM_IMG}
	kubectl apply -f cored.yaml

delete: ## Delete Kontaind from cluster
	./deploy.sh  delete

clean: ## Clean up local build dir
	rm -rf ${BLDDIR}

TEST_SPEC ?= ${CURDIR}/tests/dweb-service.yaml
TEST_POD ?= dweb

test: ## Basic test - run 'km dweb' and make sure it comes up as a pod
	-kubectl delete -f ${TEST_SPEC}
	kubectl create -f ${TEST_SPEC}
	kubectl wait --for=condition=Ready pod/${TEST_POD} --timeout=1m
	kubectl logs ${TEST_POD}
	-kubectl delete -f ${TEST_SPEC}

include ${TOP}/make/images.mk
