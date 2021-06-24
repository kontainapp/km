#  Copyright © 2018-2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# The image will be used to install kontain artifacts on each node.

TOP := $(shell git rev-parse --show-toplevel)

COMPONENT = kontain-installer
RELEASE_TAG := ${COMPONENT}

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
