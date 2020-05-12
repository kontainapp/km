# Copyright © 2018-2020 Kontain Inc. All rights reserved.
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
		km_guest_asmcode.s km_snapshot.c km_exec.c km_fork.c
VERSION_SRC := km_main.c # it has branch/version info, so rebuild it if git info changes
INCLUDES := ${TOP}/include
LLIBS := elf z
COVERAGE := yes
LOCAL_COPTS := -Werror
LOCAL_LDOPTS := -Wl,--script=km_guest_ldcmd

include ${TOP}/make/actions.mk

${BLDDIR}/km_guest_asmcode.o: CFLAGS += -fPIC
