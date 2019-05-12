# Copyright © 2018 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# create/destroy stuff in azure, an deploy to kube there
# TODO : rework apply -f, it is a hack for now

TOP := $(shell git rev-parse --show-cdup)
include cloud_config.mk

help:
	@echo 'please specify explicit target. In bash, "make <tab>" shows the list'

resources:
	./create_initial_resources.sh

K8S_KVM_DEVICE_PLUGIN := ${TOP}cloud/k8s/kvm-ds.yml
cluster:
	./create_kube_cluster.sh
	kubectl apply -f ${K8S_KVM_DEVICE_PLUGIN}

cleancluster:
	-./clear_kube_cluster.sh

cleanall: cleancluster cleanresources

cleanresources:
	-./clear_initial_resources.sh

clean: cleanall

distro publish:
	$(MAKE) $(MAKEFLAGS) -C ../..  $@

# Support for simple debug print (make debugvars)
VARS_TO_PRINT ?= CLOUD_RESOURCE_GROUP CLOUD_SUBSCRIPTION CLOUD_LOCATION \
					K8_SERVICE_PRINCIPAL K8S_CLUSTER K8S_NODE_INSTANCE_SIZE \
					REGISTRY_NAME  REGISTRY REGISTRY_AUTH_EXAMPLE REGISTRY_SKU

.PHONY: debugvars
debugvars:   ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))