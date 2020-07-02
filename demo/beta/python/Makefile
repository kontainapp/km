
TOP := $(shell git rev-parse --show-toplevel)
CURRENT_DIR := ${TOP}/demo/beta/python

PYTHON_VERSION := 3.8
IMAGE_VERSION := ${PYTHON_VERSION}
IMAGE_NAME := docker.io/kontainapp/python
IMAGE_TAG := ${IMAGE_NAME}:${IMAGE_VERSION}

SUBDIRS := flask django
TARGETS := $(foreach subdir,${SUBDIRS},$(addprefix ${subdir}/,docker kontain))
CLEAN_TARGETS := $(foreach subdir,${SUBDIRS}, $(addprefix ${subdir}/,clean))

.PHONY: all base ${TARGETS}
all: base ${TARGETS}

base: ## Build a release image for beta/python
	$(call clean_container_image,${IMAGE_TAG})
	mkdir -p ${BLDDIR}
	cp Dockerfile ${BLDDIR}/Dockerfile
	cp ${KM_BIN} ${BLDDIR}/km
	cp pyvenv.cfg ${BLDDIR}/pyvenv.cfg
	${DOCKER_BUILD} --build-arg VERS=${PYTHON_VERSION} -t ${IMAGE_TAG} ${BLDDIR}

.PHONY: clean ${CLEAN_TARGETS}
clean: ${CLEAN_TARGETS} ## Clean all the images built
	$(call clean_container_image,${IMAGE_TAG})
	rm -rf ${BLDDIR}

define CONPONENTS_SUB_MAKEFILE
COMPONENT := ${1}
include components.mk
endef

$(foreach subdir,$(SUBDIRS),$(eval $(call CONPONENTS_SUB_MAKEFILE,${subdir})))

include ${TOP}/make/locations.mk

