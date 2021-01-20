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
CURRENT_DIR := ${TOP}/cloud/k8s/kontaind

COMPONENT = kontain-installer
RELEASE_TAG := ${COMPONENT}
RUNENV_PATH = ${BLDDIR}

# runenv-image configs
export define runenv_prep
	mkdir -p ${RUNENV_PATH}/kontain/bin && cp ${KM_BIN} $$_
	mkdir -p ${RUNENV_PATH}/kontain/runtime && cp ${KM_LDSO} $$_
	cp installer.sh ${RUNENV_PATH}/installer.sh
endef

.DEFAULT_GOAL := runenv-image


# TODO: set the right image and version once we have over own device plugin
DEVICE_PLUGIN_KVM_IMG ?= quay.io/kubevirt/device-plugin-kvm:latest

.PHONY: deploy delete test clean

DEPLOY_SCRIPT := ${CURRENT_DIR}/deploy.sh
deploy: ## Deploy Kontaind to target Kubernetes cluster
	${DEPLOY_SCRIPT} deploy ${RUNENV_IMG_REG}:${IMAGE_VERSION} ${DEVICE_PLUGIN_KVM_IMG}

delete: ## Delete Kontaind from cluster
	${DEPLOY_SCRIPT} delete

clean: ## Clean up local build dir
	rm -rf ${RUNENV_PATH}

TEST_SPEC ?= ${CURRENT_DIR}/tests/dweb-service.yaml
TEST_POD ?= dweb

test: ## Basic test - run 'km dweb' and make sure it comes up as a pod
	-kubectl delete -f ${TEST_SPEC}
	kubectl create -f ${TEST_SPEC}
	kubectl wait --for=condition=Ready pod/${TEST_POD} --timeout=1m
	kubectl logs ${TEST_POD}
	-kubectl delete -f ${TEST_SPEC}

include ${TOP}/make/images.mk
