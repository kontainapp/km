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
# setenv or pass credentials as argument, currently ~/.aws is assumed
# add to nightly CI as a final step. Add info on how to use to km-releases; update AWS stuff there

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/actions.mk

all: packer

# AMI name to create. Existing AMI with name will be deleted
AMI_NAME ?= Kontain_ubuntu_20.04

packer: ${BLDDIR} ## Create AWS AMI with Ubuntu 20 + KOntain using Packer
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${TOP} kkm-pkg
	packer build \
		-var 'kkm_installer=${BLDTOP}/kkm.run' \
		-var 'target_ami_name=${AMI_NAME}' \
		-var 'summary_manifest=${BLDDIR}/packer-manifest.json' \
		ubuntu-20.04-aws.json

# Helpers to run , find and terminate test instances, for manual work

INSTANCE_TYPE ?= t2.micro
KEY_PAIR ?= aws-kkm
run-instance: ## helper to run an instance and print out ssh string when ready. Use KEY_PAIR and INSTANCE_TYPE to change default
	./scripts/run-instance.sh $(INSTANCE_TYPE) $(KEY_PAIR)

# Note: name is set in run-instance-sh
describe-instances: ## helper to kill all instances with a given mame
	@aws ec2 describe-instances \
		--filters \
				'Name=tag:Name,Values=test-Kontain_ubuntu_20.04' \
				'Name=instance-state-name,Values=running' \
		--query 'Reservations[].Instances[].InstanceId' --output text

terminate-instances: describe-instances
	@echo Manually run: 'aws ec2 terminate-instances --instance-ids <ids>'
