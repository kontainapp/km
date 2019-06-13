#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

TOP := $(shell git rev-parse --show-cdup)

# SUBDIRS := idt

LIB := runtime

# following it to compute SOURCES, picking up all .c files from relevant dirs
# (i.e. skipping SKIP_SRC_DIR). Then drop the base implementations for the sake of
# arch specific ones based on ARCH.
SRCDIR := musl/
ARCH := x86_64
SKIP_SRC_DIR := $(addprefix $(SRCDIR)src/, ipc ldso mq process sched)
SRC_DIRS := $(filter-out $(addsuffix /,${SKIP_SRC_DIR}), $(wildcard $(addprefix $(SRCDIR),src/*/)))

KM_REPLACED_SRCS := syscall.s __libc_start_main.c __init_tls.c pthread_create.c pthread_join.c uname.c
KM_EXTRA_SRCS := start_c_km.c syscall_km.c syscall_cp_km.c pthread_create_km.c pthread_exit_km.c pthread_join_km.c \
					uname_km.c runtime_stub_km.c

BASE_SRCS := $(wildcard $(addsuffix *.c,$(SRC_DIRS)))
ARCH_SRCS := $(wildcard $(addsuffix $(ARCH)/*.[csS],$(SRC_DIRS)))
# asm/arch specific implementations replacing base ones
REPLACED_SRCS := $(addsuffix .%, $(subst /$(ARCH)/,/,$(basename $(ARCH_SRCS))))
SOURCES := $(filter-out ${REPLACED_SRCS}, ${BASE_SRCS} ${ARCH_SRCS})
SOURCES := $(filter-out $(addprefix %, ${KM_REPLACED_SRCS}), ${SOURCES}) ${KM_EXTRA_SRCS}

H_DIRS := arch/$(ARCH) arch/generic src/include src/internal src/errno include
# has to be recursive assignment as BLDDIR is defined in an include below
INCLUDES = . ${TOP}include $(addprefix ${SRCDIR}, ${H_DIRS}) ${BLDDIR}${SRCDIR}include

CWERRS := -Werror=implicit-function-declaration -Werror=implicit-int -Werror=pointer-sign -Werror=pointer-arith
CWARNS := -Wno-parentheses -Wno-uninitialized -Wno-missing-braces -Wno-unused-value -Wno-unused-but-set-variable \
		  -Wno-unknown-pragmas -Wno-pointer-to-int-cast

# -ffunction-sections -fdata-sections work together with linker script, see km.ld
# Other -f... options are to reduce generated code size
COPTS := -Os -D_XOPEN_SOURCE=700 -pipe -nostdinc \
	-fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -fexcess-precision=standard -frounding-math \
	-fno-tree-loop-distribute-patterns -fno-stack-protector \
	${CWERRS} ${CWARNS}

include ${TOP}make/actions.mk

${DEPS}: | $(addprefix ${BLDDIR}${SRCDIR}include/, bits/alltypes.h bits/syscall.h version.h)

${BLDDIR}${SRCDIR}include/bits/alltypes.h: $(SRCDIR)arch/$(ARCH)/bits/alltypes.h.in $(SRCDIR)include/alltypes.h.in
	mkdir -p $(dir $@)
	sed -f $(SRCDIR)tools/mkalltypes.sed $(SRCDIR)arch/$(ARCH)/bits/alltypes.h.in $(SRCDIR)include/alltypes.h.in > $@

${BLDDIR}${SRCDIR}include/bits/syscall.h: $(SRCDIR)arch/$(ARCH)/bits/syscall.h.in
	mkdir -p $(dir $@)
	cp $< $@
	sed -n -e s/__NR_/SYS_/p < $< >> $@

${BLDDIR}${SRCDIR}include/version.h: $(wildcard $(SRCDIR)VERSION $(SRCDIR).git)
	mkdir -p $(dir $@)
	printf '#define VERSION "%s"\n' "$$(cd $(SRCDIR); sh tools/version.sh)" > $@
