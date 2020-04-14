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

SHELL=/bin/bash
TOP := $(shell git rev-parse --show-toplevel)
include cloud_config.mk

default:  help

resources:  ## Provision Azure resources (resource group and account)
	./create_initial_resources.sh

KONTAIND_DIR := ${TOP}/cloud/k8s/kontaind
cluster: ## Provision Kubernetes cluster on pre-provisioned Resource Group
	./create_kube_cluster.sh
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${KONTAIND_DIR} install

cleancluster: ## Delete Kubernetes cluster
	-./clear_kube_cluster.sh

cleanall: cleancluster cleanresources

cleanresources: ## delete all Azure resources in resource group (defined in cloud_config.mk)
	-./clear_initial_resources.sh

clean: cleanall  ## clean cluster and resources

distro publish:  ## Scan KM repo and build / publish all containers
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ../..  $@


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

export define kubectl_install_message
echo -e "${RED}kubectl CLI is not installed. ${NOCOLOR}"
cat <<EOF
# To install on Fedora:
sudo dnf install kubernetes-client
EOF
endef

login:  .check_tools ## Interactive login to Azure, Azure Container Registry and Kubernetes cluster on Azure
	az login -o table
	az acr login -n $(REGISTRY_NAME)
	az aks get-credentials --resource-group $(CLOUD_RESOURCE_GROUP) --name $(K8S_CLUSTER) --overwrite-existing

login-cli: .check_tools  ## Non-interactive login to Azure, Azure Container Registry and Kubernetes cluster on Azure
	@echo az login ${SP_DISPLAYNAME}
	@az login --service-principal -u ${SP_APPID} -p ${SP_PASSWORD} --tenant ${SP_TENANT} -o table
	az acr login -n $(REGISTRY_NAME)
	az aks get-credentials --resource-group $(CLOUD_RESOURCE_GROUP) --name $(K8S_CLUSTER) --overwrite-existing

.check_tools:
	@if ! which az > /dev/null ; then \
		eval "$$az_install_message" ; \
		exit 1; \
	fi
	@if ! which kubectl > /dev/null ; then \
		eval "$$kubectl_install_message" ; \
		exit 1; \
	fi

# Support for manually purging old test images
# max age for purge, in 'duration string' format https://golang.org/pkg/time/
CI_IMAGE_PURGE_AGE ?= 3d
CI_IMAGE_REPOS_TO_PURGE := \
	test-km-fedora \
	test-node-fedora \
	test-python-fedora \
	test-jdk-11.0.6-fedora \
	demo-runenv-dweb \
	demo-runenv-python \
	demo-runenv-node \
	demo-runenv-jdk-11.0.6 \
	runenv-kontain-installer
CI_IMAGE_DRY_RUN ?= --dry-run
CI_IMAGE_PURGE_CMD="mcr.microsoft.com/acr/acr-cli:0.1 purge --registry {{.Run.Registry}} \
	--filter '${repo}:.*' --untagged ${CI_IMAGE_DRY_RUN} --ago ${CI_IMAGE_PURGE_AGE}"

CI_BUILDENV_IMAGE_VERSION ?= latest
ci-image-purge: ## purge CI test images older that CI_IMAGE_PURGE_AGE
ifneq ($(CI_IMAGE_DRY_RUN),)
	@echo -e "${GREEN}Doing dry run. To do actual purge, run with CI_IMAGE_DRY_RUN=\"\" ${NOCOLOR}"
endif
	@echo "Purging images for $(CI_IMAGE_REPOS_TO_PURGE) older than $(CI_IMAGE_PURGE_AGE)"
	$(foreach repo,${CI_IMAGE_REPOS_TO_PURGE}, az acr run --cmd '${CI_IMAGE_PURGE_CMD}' --registry ${REGISTRY_NAME} /dev/null; )

# Helpers to avoid cut-n-paste in pipeline definition.
ci-prepare-testenv:
	make -C $(TOP)/$(LOCATION) pull-buildenv-image clean all testenv-image push-testenv-image

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

include $(TOP)/make/locations.mk

