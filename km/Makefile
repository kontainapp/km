# Copyright © 2018-2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

TOP := $(shell git rev-parse --show-cdup)

EXEC := km
SOURCES := load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c km_mem.c km_mmap.c \
		km_gdb_stub.c gdb_kvm_x86_64.c km_signal.c km_init_guest.c km_intr.c km_coredump.c \
		km_filesys.c km_unittest.c km_hc_name.c
VERSION_SRC := km_main.c # it has branch/version info, so rebuild it if git info changes
INCLUDES := ${TOP}include
LLIBS := elf z
COVERAGE := yes

include ${TOP}make/actions.mk

#
# Building Docker Image with KM (and related shared libs), and running sanity check.
# This is deliberately separated from payloads and distro.mk - we may decide to change packaging for
# KM and, say, distribute it as RPM with statically linked KM, or single binary, or whatever.
#
# For now, build as a base layer for Docker image, so we can guarantee the latest and greatest KM
# in any containarized test/demo

KM_IMAGE_LABEL := LABEL version="1.0" maintainer="Mark Sterin <msterin@kontain.app>"
KM_IMAGE_MESSAGE := Kontain Monitor Base Image
# Docker image name for KM
# Example is 'kontain/km:msterin-mmap-basic', where current branch is msterin/mmap-basic
KM_IMAGE_FULL_NAME := kontain/$(EXEC):$(IMAGE_VERSION)

# Build  a docker image with KM only, to be used as base image if needed
# We build KM static, put it in a bare docker container, and validate it
distro: ${BLDEXEC}
	@$(TOP)make/check-docker.sh
	@echo "Creating docker image '$(KM_IMAGE_FULL_NAME)'..." ; docker rmi -f $(KM_IMAGE_FULL_NAME) 2>/dev/null
	tar -C $(dir $(BLDEXEC)) -c $(notdir $(BLDEXEC)) | docker import -m '$(KM_IMAGE_MESSAGE)' -c '$(KM_IMAGE_LABEL)' - $(KM_IMAGE_FULL_NAME)
	@echo -e "Docker image created: $(GREEN)`docker image ls $(KM_IMAGE_FULL_NAME) --format '{{.Repository}}:{{.Tag}} Size: {{.Size}} sha: {{.ID}}'`$(NOCOLOR)"

distroclean:
	-docker rmi -f ${KM_IMAGE_FULL_NAME}


.PHONY: install
install: ${KM_INSTALL_BIN}/${EXEC}

 ${KM_INSTALL_BIN}/${EXEC} : ${BLDEXEC}
	@echo Installing to KM_INSTALL_HOME=$(KM_INSTALL_HOME)
	mkdir -p ${KM_INSTALL_BIN}
	cp $(BLDEXEC) ${KM_INSTALL_BIN}
	@if ! [[ "$$PATH" == *"~/.local/kontain/bin"* ]] ; then \
		echo -e '$(GREEN)\nDo not forget to add ${KM_INSTALL_BIN} to PATH, e.g. add this to ~/.bashrc: $(NOCOLOR)' ; \
		echo -e 'if ! [[ "$$PATH" == *"${KM_INSTALL_BIN}"* ]] ; then PATH="$$PATH:${KM_INSTALL_BIN}" ; fi\n' ; \
	fi