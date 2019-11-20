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
# A helper include for makefiles which need to build disto packages or publish to registries.#

include $(TOP)make/locations.mk

# Image name for KM, build in KM dir.
KM_IMAGE_FULL_NAME := kontain/km:$(IMAGE_VERSION)
# Dockerfie has it hardcoded and I did not find a way to pass --build-args to Dockerfile's 'FROM'
KM_IMAGE_EXPECTED := kontain/km:current

ifeq ($(PAYLOAD_IMAGE_SHORT_NAME),)
$(error "Please define PAYLOAD_IMAGE_SHORT_NAME in your makefile)
endif

# this is how we will name images locally (we will add :<tag> later)
PAYLOAD_IMAGE_NAME = kontain/${PAYLOAD_IMAGE_SHORT_NAME}
# tag used in remote registry. We assume 'docker login' is done outside of the makefiles
PUBLISH_TAG = $(subst kontain,$(REGISTRY),$(PAYLOAD_IMAGE_NAME))

# shortcuts for using below
PAYLOAD_BRANCH = ${PAYLOAD_IMAGE_NAME}:${IMAGE_VERSION}
PAYLOAD_LATEST = ${PAYLOAD_IMAGE_NAME}:latest
PUBLISH_BRANCH = ${PUBLISH_TAG}:${IMAGE_VERSION}
PUBLISH_LATEST = ${PUBLISH_TAG}:latest

# Build KM payload container, using KM container as the base image
TAR_FILE := payload.tar
DOCKERFILE_CONTENT := \
	FROM $(KM_IMAGE_FULL_NAME) \\n \
	LABEL Description=\"${PAYLOAD_NAME} \(/${PAYLOAD_KM}\) in Kontain\" Vendor=\"Kontain.app\" Version=\"0.1\"	\\n \
	ADD $(TAR_FILE) / \\n \
	WORKDIR /$(dir $(PAYLOAD_KM)) \\n \
	ENTRYPOINT [ \"/km\"]

distro: all
	@$(TOP)make/check-docker.sh
	@tar -cf $(TAR_FILE) $(PAYLOAD_FILES)
	@-echo "Cleaning old image"; \
		docker rmi -f ${PAYLOAD_BRANCH} ${PAYLOAD_LATEST} 2>/dev/null
	@echo -e "Building new image with generated Dockerfile:\\n $(DOCKERFILE_CONTENT)"
	@echo -e $(DOCKERFILE_CONTENT) | docker build --force-rm -t $(PAYLOAD_BRANCH) -f - .
	docker tag ${PAYLOAD_BRANCH} ${PAYLOAD_LATEST}
	@rm $(TAR_FILE)
	@echo -e "Docker image(s) created: \n$(GREEN)`docker image ls $(PAYLOAD_IMAGE_NAME) --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"

distroclean:
	-docker rmi -f ${PAYLOAD_BRANCH} ${PAYLOAD_LATEST}

publish:
	@echo Tagging and pushing to Docker registry as ${PUBLISH_TAG}.
	@echo Do not forget to authenticate to the registry, e.g. "$(REGISTRY_AUTH_EXAMPLE)".
	docker tag ${PAYLOAD_LATEST} ${PUBLISH_LATEST}
	docker push ${PUBLISH_LATEST}
	-docker rmi  ${PUBLISH_LATEST}

publishclean:
	-${CLOUD_SCRIPTS}/untag_image.sh $(PAYLOAD_IMAGE_SHORT_NAME) $(IMAGE_VERSION)
	-${CLOUD_SCRIPTS}/untag_image.sh $(PAYLOAD_IMAGE_SHORT_NAME)latest


.DEFAULT:
	@echo $(notdir $(CURDIR)): ignoring target '$@'

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


# Support for simple debug print (make debugvars)
VARS_TO_PRINT ?= PAYLOAD_IMAGE_NAME IMAGE_VERSION PUBLISH_TAG PUBLISH_BRANCH PUBLISH_LATEST REGISTRY_NAME REGISTRY REGISTRY_AUTH_EXAMPLE CLOUD

.PHONY: debugvars
debugvars:   ## prints interesting vars and their values
	@echo To change the list of printed vars, use 'VARS_TO_PRINT="..." make debugvars'
	@echo $(foreach v, ${VARS_TO_PRINT}, $(info $(v) = $($(v))))

# allows to do 'make print-varname'
print-%  : ; @echo $* = $($*)