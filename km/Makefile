# Copyright © 2018-2021 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.

TOP := $(shell git rev-parse --show-toplevel)

EXEC := km
SOURCES := load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c km_mem.c km_mmap.c \
		km_gdb_stub.c gdb_kvm_x86_64.c km_signal.c km_init_guest.c km_intr.c km_coredump.c \
		km_filesys.c km_hc_name.c km_trace.c km_musl_related.c km_decode.c km_proc.c \
		km_guest_asmcode.s km_snapshot.c km_exec.c km_fork.c km_management.c \
		km_kkm.c km_vmdriver.c km_sid_pgid.c
VERSION_SRC := km_main.c # it has branch/version info, so rebuild it if git info changes
INCLUDES := ${TOP}/include
LLIBS := elf z
COVERAGE := yes
LOCAL_COPTS := -Werror -D_GNU_SOURCE
LOCAL_LDOPTS := -Wl,--script=km_guest_ldcmd

include ${TOP}/make/actions.mk

${BLDDIR}/km_guest_asmcode.o: CFLAGS += -fPIC

# Package KM and libs+tools+docs for release
KM_RELEASE := ${BLDTOP}/kontain.tar.gz
# default tag
RELEASE_TAG ?= v0.1-test
# Show this on the release page
RELEASE_MESSAGE ?= Kontain KM Beta Release. branch: ${SRC_BRANCH} sha: ${SRC_SHA}


release: ${KM_RELEASE}
	ls -lh ${KM_RELEASE} ${KM_BIN}

${KM_RELEASE}: ${KM_BIN} ## Build a release tar.gz file for KM
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${TOP}/tools/faktory
	${TOP}/tools/bin/create_release.sh


publish-release:
	cd ${TOP}/tools/release; ./release_km.py ${KM_RELEASE} --version ${RELEASE_TAG} --message "${RELEASE_MESSAGE}"
