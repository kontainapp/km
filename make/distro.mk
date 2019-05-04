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
# A helper include for makefiles which need to build disto packages or publish to registries.#

include $(TOP)make/locations.mk

# Image name for KM, build in KM dir.
KM_IMAGE_FULL_NAME := kontain/km:$(IMAGE_VERSION)
# Dockerfie has it hardcoded and I did not find a way to pass --build-args to Dockerfile's 'FROM'
KM_IMAGE_EXPECTED := kontain/km:current

ifeq ($(PAYLOAD_IMAGE_SHORT_NAME),)
  $(error "Please define PAYLOAD_IMAGE_SHORT_NAME in your makefile)
endif

# this is how we will name images locally
PAYLOAD_IMAGE_FULL_NAME := kontain/$(PAYLOAD_IMAGE_SHORT_NAME):$(IMAGE_VERSION)
# tag used in remote registry. We assume 'docker login' is done outside of the makefiles
PUBLISH_TAG := $(subst kontain,$(REGISTRY),$(PAYLOAD_IMAGE_FULL_NAME))

# Build KM payload container, using KM container as the base image
TAR_FILE := payload.tar
DOCKERFILE_CONTENT := \
	FROM $(KM_IMAGE_FULL_NAME) \\n \
	LABEL Description=\"${PAYLOAD_NAME} \(/${PAYLOAD_KM}\) in Kontain\" Vendor=\"Kontain.app\" Version=\"0.1\"	\\n \
	ADD $(TAR_FILE) / \\n \
	ENTRYPOINT [ \"/km\"] \\n

distro: all
	@tar -cf $(TAR_FILE) $(PAYLOAD_FILES)
	@echo "Cleaning old image"; docker rmi -f $(PAYLOAD_IMAGE_FULL_NAME) 2>/dev/null
	@echo -e $(DOCKERFILE_CONTENT) | docker build --force-rm -t $(PAYLOAD_IMAGE_FULL_NAME) -f - .
	@rm $(TAR_FILE)
	@echo -e "Docker image created: $(GREEN)`docker image ls $(PAYLOAD_IMAGE_FULL_NAME) --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"

distroclean:
	docker rmi -f ${PAYLOAD_IMAGE_FULL_NAME}

publish:
	@echo Tagging and pushing to Docker registry as ${PUBLISH_TAG}.
	@echo Do not forget to authenticate to the registry, e.g. "$(REGISTRY_AUTH_EXAMPLE)".
	docker tag ${PAYLOAD_IMAGE_FULL_NAME} ${PUBLISH_TAG}
	docker push ${PUBLISH_TAG}
	docker rmi $(PUBLISH_TAG)

publishclean:
	-${CLOUD_SCRIPTS}/untag_image.sh $(PAYLOAD_IMAGE_SHORT_NAME) $(IMAGE_VERSION)

.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'

VAR1 = $(shell $(TOP)/cloud/$(CLOUD)/get_config )
# Support for simple debug print (make debugvars)
VARS_TO_PRINT ?= PAYLOAD_IMAGE_FULL_NAME PUBLISH_TAG REGISTRY_NAME REGISTRY REGISTRY_AUTH_EXAMPLE CLOUD

.PHONY: debugvars
debugvars:   ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))