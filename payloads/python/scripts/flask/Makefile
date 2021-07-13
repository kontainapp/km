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
# Build and run python flask example (targets build* and run*)
# Convert to Kontain and test (targets faktory and test)
#
# pass DTYPE=type for different distros (see DTYPES below for the list)

TOP := $(shell git rev-parse --show-toplevel)

# test label is used only to simplify searching for containers build in this makefile
TEST_LABEL := flask-km-test
override DOCKER_RUN := docker run ${DOCKER_RUN_CLEANUP} -ti  -p 8080:8080 --label $(TEST_LABEL)

# supported types
DTYPES := alpine fedora ubuntu
# default type
DTYPE ?= alpine

IMAGE_NAME := flask
IMAGE :=  $(IMAGE_NAME):$(DTYPE)

KM_IMAGE_NAME := flask-km
# note: KM_IMAGE for different DTYPES is pretty much identical (sans .py location). Keeping all for testing
KM_IMAGE := $(KM_IMAGE_NAME):$(DTYPE)

ifeq ($(DTYPE),alpine)
	UPDATE_INDEX := true
	# we need bash for scripts in faktory too
	ADD_PIP3 := apk add py3-pip bash
endif

ifeq ($(DTYPE),fedora)
	UPDATE_INDEX := dnf -y update
	ADD_PIP3 := dnf -y install python3-pip
endif

ifeq ($(DTYPE),ubuntu)
	# ADD_PIP3 := apt-get update -y && apt-get install -y python3-pip
	UPDATE_INDEX := apt-get update -y
	ADD_PIP3 := apt-get install -y python3-pip
endif

ifeq ($(ADD_PIP3),)
  $(error "In DTYPE=type, the supported types are: '$(DTYPES)'")
endif

build: ## build python flask example, based on alpine. alpine can we swapped for fedora or ubuntu by passing DTYPE=type (alpine, fedora, ubuntu)
	$(DOCKER_BUILD) \
		--build-arg distro="$(DTYPE)" --build-arg add_pip3="$(ADD_PIP3)" --build-arg update_index="$(UPDATE_INDEX)" \
		-t $(IMAGE) .

run: ## run python flask example,based on alpine. Same DTYPE as supported as in'build'. Assumes "build" was successful.
	$(DOCKER_RUN) $(IMAGE)

faktory: ${KM_BIN} ## Convert flask example to Kontainer. Same DTYPE as supported as in'build'. Assumes "build" was successful.
	./faktory_run_build.sh "$(IMAGE)" "$(KM_IMAGE)" "$(DTYPE)" "${KM_BIN}"

test:   ## run Kontain with flask example,based. Same DTYPE as supported as in'build'. Assumes "faktory" was successful.
	$(DOCKER_RUN) --device=/dev/kvm $(KM_IMAGE)

clean: ## Kill test containers (if any) and remove all images build for flask-km (python flask is untouched)
	@echo Cleaning. Ignore messages about failures and ignoring failures.
	-docker rm -f `docker ps -aq --filter label=$(TEST_LABEL)` 2> /dev/null
	-docker rmi $(KM_IMAGE)
	-docker image prune  -f

TARGET?= build
all-dtypes: # make TARGET for each DTYPE. E.g. `make all-dtypes TARGET=clean". Default is 'build'
	for t in $(DTYPES) ; do $(MAKE) MAKEFLAGS="$(MAKEFLAGS)" $(TARGET) DTYPE=$$t ; done

#
# 'Help' target - based on '##' comments in targets
#
# This target ("help") scans Makefile for '##' in targets and prints a summary
# Note - used awk to print (instead of echo) so escaping/coloring is platform independed
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
	/^[.a-zA-Z0-9_-]+[ \t]*:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo 'For specific help in folders, try "(cd <dir>; make help)"'
	@echo ""

include $(TOP)/make/locations.mk


try:
	docker image inspect $(IMAGE) -f '{{json .Config.Entrypoint }}'
	docker image inspect $(IMAGE) -f '{{json .Config.Cmd }}'
	echo '\n#-- generated lines:--\nENTRYPOINT $(SRC_ENTRYPOINT)\nCMD $(SRC_CMD)\n' | cat faktory_stem.dockerfile -
