# A basic Makefile to make KKM AWS test slightly more uniform
# (this is currently used only for KKM testing on AWS)
#
# to run locally, pass IMAGE_VERSION for an existing test pass, e.g.
# make IMAGE_VERSION=ci-146

TOP := $(shell git rev-parse --show-toplevel)

SOURCE_BRANCH ?= $(SRC_BRANCH)

LOG ?= 0
PACKER_BUILD = PACKER_LOG=${LOG} time packer build

test: .check_tools ## Test KKM build on AWS using Hashicorp Packer
	${PACKER_BUILD} \
		--var source_branch=${SOURCE_BRANCH} \
		--var image_version=${IMAGE_VERSION} \
		kkm-test.pkr.hcl

.check_tools:
	@if ! command -v packer; then \
		echo -e "${RED}Packer (https://www.packer.io/) is not found. Please install it first${NOCOLOR}" ; \
		false; \
	fi

include ${TOP}/make/locations.mk
