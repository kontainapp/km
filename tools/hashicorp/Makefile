# Copyright © 2021 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Build a kkm-based VM images using packer

# TODO
# --------
# setenv or pass credentials as argument for AWS work, currently ~/.aws is assumed
# add to nightly CI as a final step.

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/actions.mk

all: vm-images

#TODO: pass this
# PRELOAD_IMAGES := kontainapp/spring-boot-demo kontainapp/runenv-python kontainapp/runenv-node

UBUNTU_BOX ?= generic/ubuntu2010
FEDORA_BOX ?= generic/fedora32
# hack. Assume both fedora and ubuntu have the same box version for virtualbox (likely since it's front he same builder)
# note: version needs to be synced with the one in build_km_images.json
BOX_VERSION ?= 3.2.20

source-boxes:
	@if ! vagrant box list  | grep -q "${UBUNTU_BOX}.*virtualbox.*${BOX_VERSION}" ;  then \
		vagrant box add ${UBUNTU_BOX} --provider=virtualbox --box-version=${BOX_VERSION} ; \
	fi
	@if ! vagrant box list  | grep -q "${FEDORA_BOX}.*virtualbox.*${BOX_VERSION}" ;  then \
		vagrant box add ${FEDORA_BOX} --provider=virtualbox --box-version=${BOX_VERSION} ; \
	fi

LOG ?= 0
PACKER_BUILD = PACKER_LOG=${LOG} time packer build

vm-images: product source-boxes
	${PACKER_BUILD} -force -except file.debug-pp -var-file images/ubuntu2010.pkrvars.hcl images
	${PACKER_BUILD} -force -except file.debug-pp -var-file images/fedora32.pkrvars.hcl images


product:
	@if ! type virtualbox >& /dev/null ; then echo -e "${RED}'virtualbox' is missing - please install it first ${NOCOLOR}"; false; fi
	@if ! type vagrant >& /dev/null ; then echo -e "${RED}'vagrant' is missing - please install it first ${NOCOLOR}"; false; fi
	$(MAKE) MAKEFLAGS=$(MAKEFLAGS) -C ${TOP} kkm-pkg
	$(MAKE) MAKEFLAGS=$(MAKEFLAGS) -C ${TOP} release

ami:  ## Build AMI with KKM in the AWS
	${PACKER_BUILD} ubuntu-20.04-aws.pkr.hcl

test-box-upload: ## Helper to test Vagrant Box upload from existing BOX file
	${PACKER_BUILD} test_box_upload.pkr.hcl

# helpers to use old process to create boxes and AMIs and instances based on it
# =====

# local register for a box created by running vagrant up in fedora or ubuntu dirs
# Note that the box name should be `kontain/beta2-kkm`
# (it is mentioned in km-releases doc so change it there if needed),
# and provider should be virtualbox
register-box: vagrant ## upload new version to vagrantclount
	vagrant halt; vagrant package --output /tmp/beta2-kkm.box
	vagrant box add --name kontain-box-name /tmp/beta2-kkm.box

## TODO - the targets below are deprecated. TODO: add helpers for new AMI names

# AMI name to create. Existing AMI with name will be deleted
AMI_NAME ?= Kontain_ubuntu_20.04

__packer: ${BLDDIR} ## Create AWS AMI with Ubuntu 20 + KOntain using Packer
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${TOP} kkm-pkg
	packer build \
		-var 'kkm_installer=${BLDTOP}/kkm.run' \
		-var 'target_ami_name=${AMI_NAME}' \
		-var 'summary_manifest=${BLDDIR}/packer-manifest.json' \
		ubuntu-20.04-aws.json

# Helpers to run , find and terminate test instances, for manual work

INSTANCE_TYPE ?= t2.micro
KEY_PAIR ?= aws-kkm
__run-instance: ## helper to run an instance and print out ssh string when ready. Use KEY_PAIR and INSTANCE_TYPE to change default
	./scripts/run-instance.sh $(INSTANCE_TYPE) $(KEY_PAIR)

# Note: name is set in run-instance-sh
__describe-instances: ## helper to kill all instances with a given mame
	@aws ec2 describe-instances \
		--filters \
				'Name=tag:Name,Values=test-Kontain_ubuntu_20.04' \
				'Name=instance-state-name,Values=running' \
		--query 'Reservations[].Instances[].InstanceId' --output text

terminate-instances: describe-instances
	@echo Manually run: 'aws ec2 terminate-instances --instance-ids <ids>'


