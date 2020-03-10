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
CURRENT := ${TOP}/cloud/k8s/kontaind

COMPONENT = kontain-installer
IMAGE_VERSION = latest
CLOUD ?= dockerhub

INSTALL_CONFIG ?= kontaind.yaml

.PHONY: install
install:
	kubectl create -f ${INSTALL_CONFIG}

.PHONY: uninstall
uninstall:
	kubectl delete -f ${INSTALL_CONFIG}

TEST_SPEC ?= ${CURRENT}/tests/dweb-service.yaml
TEST_POD ?= dweb

.PHONY: test
test:
	-kubectl delete -f ${TEST_SPEC}
	kubectl create -f ${TEST_SPEC}
	kubectl wait --for=condition=Ready pod/${TEST_POD} --timeout=10s
	-kubectl delete -f ${TEST_SPEC}

include ${TOP}/make/images.mk
.PHONY: runenv-image
runenv-image:
	@$(TOP)/make/check-docker.sh
	-docker rmi -f ${RUNENV_IMG}:${IMAGE_VERSION} 2>/dev/null
	mkdir -p ${RUNENV_PATH}/bin
	cp ${KM_BIN} ${RUNENV_PATH}/bin/km
	${DOCKER_BUILD} -t ${RUNENV_IMG}:${IMAGE_VERSION} -f ${CURRENT}/installer.dockerfile ${RUNENV_PATH}
