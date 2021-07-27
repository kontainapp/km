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
# create/destroy stuff in azure, an deploy to kube there
# TODO : rework apply -f, it is a hack for now

TOP := $(shell git rev-parse --show-toplevel)
include cloud_config.mk

default:  help

resources:  ## Provision Azure resources (resource group and account)
	./create_initial_resources.sh

cluster: ## Provision Kubernetes cluster on pre-provisioned Resource Group
	./create_kube_cluster.sh
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${TOP}/cloud/k8s/kontaind deploy

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
	az login -t "$(AZ_TENANT_ID)" -o table
	@#one time config, but it's cheap so let's do it every time
	az account set -s ${CLOUD_SUBSCRIPTION}
	az configure --defaults location=${CLOUD_LOCATION}
	@# container registry and kubernetes service logins. note: AKS login is long-lived
	az acr login -n "$(REGISTRY_NAME)"
	az aks get-credentials --resource-group "$(CLOUD_RESOURCE_GROUP)" --name "$(K8S_CLUSTER)" --overwrite-existing

login-cli: .check_tools  ## Non-interactive login to Azure, Azure Container Registry and Kubernetes cluster on Azure
	@echo az login "${SP_DISPLAYNAME}"
	@az login --service-principal -u "${SP_APPID}" -p "${SP_PASSWORD}" --tenant "${SP_TENANT}" -o table
	az acr login -n "$(REGISTRY_NAME)"
	az aks get-credentials --resource-group "$(CLOUD_RESOURCE_GROUP)" --name "$(K8S_CLUSTER)" --overwrite-existing

.check_tools:
	@if ! which az > /dev/null ; then \
		eval "$$az_install_message" ; \
		exit 1; \
	fi
	@if ! which kubectl > /dev/null ; then \
		eval "$$kubectl_install_message" ; \
		exit 1; \
	fi

# Support for manually purging old test and demo-runenv images
# max age for purge, in 'duration string' format https://golang.org/pkg/time/
CI_IMAGE_PURGE_AGE ?= 3d
CI_IMAGE_NAMES := busybox dweb dynamic-base dynamic-base-large dynamic-python jdk-11.0.8 node python
CI_IMAGE_REPOS_TO_PURGE := test-km-fedora $(patsubst %,test-%-fedora,$(CI_IMAGE_NAMES)) $(patsubst %,demo-runenv-%,$(CI_IMAGE_NAMES)) runenv-kontain-installer
CI_IMAGE_DRY_RUN ?= --dry-run
# "image purge" command to execute on each registry. We *ONLY* clean up images tagged as ci-*
CI_IMAGE_PURGE_CMD=mcr.microsoft.com/acr/acr-cli:0.1 purge --registry {{.Run.Registry}} \
	--untagged ${CI_IMAGE_DRY_RUN} --ago ${CI_IMAGE_PURGE_AGE}

CI_BUILDENV_IMAGE_VERSION ?= latest
ci-image-purge: ## purge CI test images older that CI_IMAGE_PURGE_AGE
ifneq ($(CI_IMAGE_DRY_RUN),)
	@echo -e "${GREEN}Doing dry run. To do actual purge, run with CI_IMAGE_DRY_RUN=\"\" ${NOCOLOR}"
endif
	@echo "Purging images for $(CI_IMAGE_REPOS_TO_PURGE) older than $(CI_IMAGE_PURGE_AGE)"
	az acr run --cmd '"${CI_IMAGE_PURGE_CMD} $(patsubst %,--filter %:ci-.*,${CI_IMAGE_REPOS_TO_PURGE})"' --registry ${REGISTRY_NAME} /dev/null;

# Helpers to avoid cut-n-paste in pipeline definition.
ci-prepare-testenv: ## helper for Azure CI. Obsolete for GitHub workflows
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

# Create base image for Vagrant operation, and Vagrant boxes creation using Azure
# Note: to re-create the base image, pass PACKER_FLAGS='-force'
PACKER_FLAGS ?= -force

# TODO - reconcile cut-n-paste for aws and azure Makefiles. Maybe move to actions.mk ?
LOG ?= 0
PACKER_BUILD = PACKER_LOG=${LOG} time packer build

L0_VMIMAGE_NAME ?= L0BaseImage
L0-image: ## build the very base image
	${PACKER_BUILD} -force -var image_name=${L0_VMIMAGE_NAME} L0-image.pkr.hcl

vagrant-box:
	${PACKER_BUILD}  -force \
		--var src_branch=${SRC_BRANCH} \
		--var image_version=${IMAGE_VERSION} \
		vagrant-box.pkr.hcl

include $(TOP)/make/locations.mk

