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
# Build a kkm-based VM images using packer

TOP := $(shell git rev-parse --show-toplevel)
include ${TOP}/make/actions.mk

all: vm-images

# Use these boxes as sources for our boxes builds. (`generic/$os' is the box)
SRC_OS := ubuntu2010 fedora32
# Note: SRC_OS needs to be synced with os_name in *pkrvars.hcl files
ARTIFACTS := ${TOP}/build/kkm.run ${TOP}/build/kontain.tar.gz

# pass PACKER_LOG=1 for debug info
PACKER_BUILD = ${PACKER_LOG} time packer build -force

product: ${ARTIFACTS} ## Prepares KM packaging artifacts (e.g. km tarball and kkm run bundle)
	echo $(MAKE) MAKEFLAGS=$(MAKEFLAGS) -C ${TOP} kkm-pkg
	echo $(MAKE) MAKEFLAGS=$(MAKEFLAGS) -C ${TOP} release

vm-images: .check_packer .check_tools product ## loads source boxes and creates boxes with KM installed
	@set -x ; for os in ${SRC_OS} ; \
	do \
		${PACKER_BUILD} -var-file box_images_templates/$$os.pkrvars.hcl box_images_templates ; \
	done

upload-boxes: .check_tools ## uploads pre-created boxes to vagrant cloud
	for os in ${SRC_OS} ; \
	do \
		vagrant cloud publish --force --no-private --release --description "$$os with Kontain installed" --short-description "$$os with KKM" \
			kontain/$${os}-kkm-beta3 $${RELEASE_TAG#v} virtualbox output_box_$${os}/package.box ; \
	done

register-boxes: ## register pre-created boxes in local vagrant cache
	@for os in ${SRC_OS} ; \
	do echo Registering $$os... ; \
		vagrant box add -f --name kontain/$${os}-kkm-beta3 output_box_$${os}/package.box ; \
	done

test-boxes:  ## Test the registered boxes
	lscpu
	@set -x; for os in ${SRC_OS} ; do \
		echo "Testing vagrant box kontain/$${os}-kkm-beta3"; \
                mkdir $${os}; \
		cd $${os}; \
		vagrant init kontain/$${os}-kkm-beta3; \
		sed --in-place -e "/^end/i\  config.vm.provider "virtualbox" do |vb|\n    vb.customize [ 'modifyvm', :id, '--cpu-profile', 'host' ]\n    vb.customize [ 'modifyvm', :id, '--nested-hw-virt', 'on' ]\n  end" Vagrantfile; \
		vagrant up; \
		TRACE=1 vagrant ssh <../scripts/box-smoke-test.sh; \
		vagrant destroy -f; \
		cd ..; \
		rm -fr $${os}; \
	done


# Note: Amazon import-image refuses to accept/import some kernel versions, so as a temporary
# bandaid we build it from existing AMI. Ideally, we just want to import a box as an AMI - see test_ami_upload.pkr.hcl
# for an examle how to do it
ami: .check_packer ## Builds AMI with KKM in the AWS
	packer init aws_ami_templates
	${PACKER_BUILD} --var release_tag=${RELEASE_TAG} aws_ami_templates

test-box-upload: ## Helper to test Vagrant Box upload from existing BOX file
	${PACKER_BUILD} test_box_upload.pkr.hcl

clean::
	rm -rf output_box_* box packer_cache

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

.check_tools:
	@if ! type virtualbox >& /dev/null ; then echo -e "${RED}'virtualbox' is missing - please install it first ${NOCOLOR}"; false; fi
	@if ! type vagrant >& /dev/null ; then echo -e "${RED}'vagrant' is missing - please install it first ${NOCOLOR}"; false; fi
