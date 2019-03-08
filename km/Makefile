# Copyright © 2018 Kontain Inc. All rights reserved.
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
		km_gdb_stub.c gdb_kvm_x86_64.c km_signal.c chan/queue.c chan/chan.c km_init_guest.c
INCLUDES := ${TOP}include
LLIBS := elf
LDOPTS ?=
COVERAGE := yes

include ${TOP}make/actions.mk

test: all
