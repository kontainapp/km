# Copyright © 2019 Kontain Inc. All rights reserved.
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

default:  help

resources:  ## Provision Azure resources (resource group and account)
	./create_initial_resources.sh

K8S_KVM_DEVICE_PLUGIN := ${TOP}cloud/k8s/kvm-ds.yml
cluster: ## Provision Kubernetes cluster on pre-provisioned Resource Group
	./create_kube_cluster.sh
	kubectl apply -f ${K8S_KVM_DEVICE_PLUGIN}

cleancluster: ## Delete Kubernetes cluster
	-./clear_kube_cluster.sh

cleanall: cleancluster cleanresources

cleanresources: ## delete all Azure resources in resource group (defined in cloud_config.mk)
	-./clear_initial_resources.sh

clean: cleanall  ## clean cluster and resources

distro publish:  ## Scan KM repo and build / publish all containers
	$(MAKE) $(MAKEFLAGS) -C ../..  $@


export define az_install_message
echo -e "${RED}Azure 'az' CLI is not installed. ${NOCOLOR}"
cat <<EOF

To install, follow the instruction on
https://docs.microsoft.com/en-us/cli/azure/install-azure-cli-yum?view=azure-cli-latest,
specifically:

# Import the Microsoft repository key.
sudo rpm --import https://packages.microsoft.com/keys/microsoft.asc
# Create local azure-cli repository information.
sudo sh -c 'echo -e "[azure-cli]
name=Azure CLI
baseurl=https://packages.microsoft.com/yumrepos/azure-cli
enabled=1
gpgcheck=1
gpgkey=https://packages.microsoft.com/keys/microsoft.asc" > /etc/yum.repos.d/azure-cli.repo'
# install
sudo dnf install -y azure-cli
EOF
endef

login:  ## Interactive login to Azure, Acure Container Registry and Kubernetes cluster on Azure
	@if ! which az >& /dev/null ; then \
		eval "$$az_install_message" ; \
		exit 1; \
	fi
	az login
	az acr login -n $(REGISTRY_NAME)
	az aks get-credentials --resource-group $(CLOUD_RESOURCE_GROUP) --name $(K8S_CLUSTER) --overwrite-existing

dashboard: login # Interactive login, and then forward port/open Kubernetes Dashboard
	az aks browse --resource-group $(CLOUD_RESOURCE_GROUP) --name $(K8S_CLUSTER)

DTYPE ?= fedora
buildenv:  ## Build km-buildenv image for Azure
	./buildenv.sh ${DTYPE}

ow: ## create vms for openwhisk testing or demo
	@./create_openwhisk_vms.sh

clearow: ## clear openwhisk machines. You can just run 'apply_openwhisk_vms.sh delete' instead
	@./apply_openwhisk_vms.sh delete

listow: ## list openwhisk machines. You can just run 'apply_openwhisk_vms.sh ls' instead
	@./apply_openwhisk_vms.sh ls

# Support for simple debug print (make debugvars)
VARS_TO_PRINT ?= CLOUD_RESOURCE_GROUP CLOUD_SUBSCRIPTION CLOUD_LOCATION \
					K8_SERVICE_PRINCIPAL K8S_CLUSTER K8S_NODE_INSTANCE_SIZE \
					REGISTRY_NAME  REGISTRY REGISTRY_AUTH_EXAMPLE REGISTRY_SKU

.PHONY: debugvars
debugvars:   ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))

include $(TOP)make/locations.mk
#
# 'Help' target - based on '##' comments in targets
#
# This target ("help") scans Makefile for '##' in targets and prints a summary
# Note - used awk to print (instead of echo) so escaping/coloring is platform independed
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
	/^[.a-zA-Z0-9_-]+:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo 'For specific help in folders, try "(cd <dir>; make help)"'
	@echo ""

