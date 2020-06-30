
TOP := $(shell git rev-parse --show-toplevel)
CURRENT_DIR := ${TOP}/demo/beta/python

PYTHON_VERSION := 3.8
IMAGE_VERSION := ${PYTHON_VERSION}
IMAGE_NAME := docker.io/kontainapp/python
IMAGE_TAG := ${IMAGE_NAME}:${IMAGE_VERSION}

all: base flask/docker flask/kontain

.PHONY: base
base: ## Build a release image for beta/python
	$(call clean_container_image,${IMAGE_TAG})
	mkdir -p ${BLDDIR}
	cp Dockerfile ${BLDDIR}/Dockerfile
	cp ${KM_BIN} ${BLDDIR}/km
	cp ${KM_LDSO} ${BLDDIR}/libc.so
	cp pyvenv.cfg ${BLDDIR}/pyvenv.cfg
	${DOCKER_BUILD} --build-arg VERS=${PYTHON_VERSION} -t ${IMAGE_TAG} ${BLDDIR}

.PHONY: clean
clean: flask/clean ## Clean all the images built
	$(call clean_container_image,${IMAGE_TAG})
	rm -rf ${BLDDIR}

include flask/subdir.mk
include ${TOP}/make/locations.mk

