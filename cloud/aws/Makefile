# placeholder for a real Makefile, just to make it slightly more uniform
# this is currently used only for KKM testing on AWS

TOP := $(shell git rev-parse --show-toplevel)

LOG ?= 0
PACKER_BUILD = PACKER_LOG=${LOG} time packer build -force

SOURCE_BRANCH ?= $(SRC_BRANCH)

# todo: pass IMAGE_VERSION so proper testenv can be fetched
test:
	@echo KKM AWS test. Branch: ${SOURCE_BRANCH} image_version: ${IMAGE_VERSION}
	SOURCE_BRANCH="${SOURCE_BRANCH}" ${PACKER_BUILD} kkm-test.pkr.hcl

include ${TOP}/make/locations.mk
