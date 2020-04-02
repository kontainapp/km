#  Copyright © 2018-2020 Kontain Inc. All rights reserved.
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

IMAGE_VERSION ?= latest
INSTALLER_IMAGE ?= kontainkubecr.azurecr.io/runenv-kontain-installer:${IMAGE_VERSION}
# TODO: set the right image and version once we have over own device plugin 
DEVICE_PLUGIN_KVM_IMAGE ?= quay.io/kubevirt/device-plugin-kvm:latest
INSTALL_SCRIPT := ${CURRENT_DIR}/install.sh

.PHONY: install
install:
	${INSTALL_SCRIPT} install ${INSTALLER_IMAGE} ${DEVICE_PLUGIN_KVM_IMAGE}

.PHONY: uninstall
uninstall:
	${INSTALL_SCRIPT} uninstall 

TEST_SPEC ?= ${CURRENT_DIR}/tests/dweb-service.yaml
TEST_POD ?= dweb

.PHONY: test
test:
	-kubectl delete -f ${TEST_SPEC}
	kubectl create -f ${TEST_SPEC}
	kubectl wait --for=condition=Ready pod/${TEST_POD} --timeout=1m
	kubectl logs ${TEST_POD}
	-kubectl delete -f ${TEST_SPEC}