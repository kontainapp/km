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

# Use these boxes as sources for our boxes builds. (`generic/$os' is the box)
SRC_OS := ubuntu2010 fedora32
# Souce box version  to pre-fetch
# Caveat: we assume both fedora and ubuntu have the same box version
# for virtualbox (it is likely since it's from the same builder)
BOX_VERSION ?= 3.2.20
# Note: version needs to be synced with the one in *pkrvars.hcl files

source-boxes: ## add boxes for ${SRC_OS} to vagrant local cache
	@set -x ; for os in ${SRC_OS} ; \
	do \
		src_box="generic/$$os" ; \
		if ! vagrant box list  | grep -q "$$src_box.*virtualbox.*${BOX_VERSION}" ;  then \
			vagrant box add $$src_box --provider=virtualbox --box-version=${BOX_VERSION} ; \
		fi \
	done

LOG ?= 0
PACKER_BUILD = PACKER_LOG=${LOG} time packer build -force

product: ## Prepares KM packaging artifacts (e.g. km tarball and kkm run bundle)
	@if ! type virtualbox >& /dev/null ; then echo -e "${RED}'virtualbox' is missing - please install it first ${NOCOLOR}"; false; fi
	@if ! type vagrant >& /dev/null ; then echo -e "${RED}'vagrant' is missing - please install it first ${NOCOLOR}"; false; fi
	$(MAKE) MAKEFLAGS=$(MAKEFLAGS) -C ${TOP} kkm-pkg
	$(MAKE) MAKEFLAGS=$(MAKEFLAGS) -C ${TOP} release

vm-images: product source-boxes ## loads source boxes and creates boxes with KM installed
	@set -x ; for os in ${SRC_OS} ; \
	do \
		${PACKER_BUILD} -only virtualbox-ovf.build-ova -var-file templates/$$os.pkrvars.hcl templates ; \
	done

upload-boxes:  ## uploads pre-created boxes to vagrant cloud
	@set -x ; for os in ${SRC_OS} ; \
	do \
		${PACKER_BUILD} -only null.upload -var-file templates/$$os.pkrvars.hcl templates ; \
	done

register-boxes: ## register pre-created boxes in local vagrant cash
	@for os in ${SRC_OS} ; \
	do echo Registering $$os... ; \
		${PACKER_BUILD} -only null.register -var-file templates/$$os.pkrvars.hcl templates ; \
	done

# Note: Amazon import-image refuses to accept/import some kernel versions, so as a temporary
# bandaid we build it from existing AMI. Ideally, we just want to import a box as an AMI - see test_ami_upload.pkr.hcl
# for an examle how to do it
ami:  ## Builds AMI with KKM in the AWS
	${PACKER_BUILD} ubuntu-20.04-aws.pkr.hcl

test-box-upload: ## Helper to test Vagrant Box upload from existing BOX file
	${PACKER_BUILD} test_box_upload.pkr.hcl

clean::
	rm -rf output_ova* box packer_cache

# Helpers to run , find and terminate test AWS instances, for manual work

INSTANCE_TYPE ?= t2.micro
KEY_PAIR ?= aws-kkm
.run-instance: ## helper to run an instance and print out ssh string when ready. Use KEY_PAIR and INSTANCE_TYPE to change default
	./scripts/run-instance.sh $(INSTANCE_TYPE) $(KEY_PAIR)

# Note: name is set in run-instance-sh
.describe-instances: ## helper to kill all instances with a given mame
	@aws ec2 describe-instances \
		--filters \
				'Name=tag:Name,Values=test-Kontain_ubuntu_20.04' \
				'Name=instance-state-name,Values=running' \
		--query 'Reservations[].Instances[].InstanceId' --output text

.terminate-instances: .describe-instances
	@echo Manually run: 'aws ec2 terminate-instances --instance-ids <ids>'


