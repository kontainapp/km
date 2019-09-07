#  Copyright © 2018-2019 Kontain Inc. All rights reserved.
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

LIB := runtime_unused

# following it to compute SOURCES, picking up all .c files from relevant dirs
# (i.e. skipping SKIP_SRC_DIR). Then drop the base implementations for the sake of
# arch specific ones based on ARCH.
SRCDIR := musl/
ARCH := x86_64
SKIP_SRC_DIR := $(addprefix $(SRCDIR)src/, ipc mq process sched)
SRC_DIRS := $(filter-out $(addsuffix /,${SKIP_SRC_DIR}), $(wildcard $(addprefix $(SRCDIR),src/*/)))

KM_REPLACED_SRCS := syscall.s syscall_cp.s __libc_start_main.c __init_tls.c __reset_tls.c  getenv.c uname.c \
					pthread_create.c pthread_join.c pthread_setcancelstate.c preadv.c pwritev.c fcntl.c procfdname.c
KM_EXTRA_SRCS := $(wildcard *_km.c) $(wildcard *.s)

KM_PTHREAD_OBJS := pthread_cond_broadcast.o pthread_cond_destroy.o pthread_cond_signal.o pthread_cond_wait.o \
						 pthread_create_km.o pthread_detach.o pthread_getspecific.o pthread_join_km.o pthread_key_create.o \
						 pthread_key_delete.o pthread_mutex_lock.o pthread_mutex_unlock.o pthread_once.o \
						 pthread_rwlock_rdlock.o pthread_rwlock_unlock.o pthread_rwlock_wrlock.o pthread_setspecific.o

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
COPTS := -Os -ggdb -D_XOPEN_SOURCE=700 -pipe -nostdinc \
	-fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -fexcess-precision=standard -frounding-math \
	-fno-tree-loop-distribute-patterns -fno-stack-protector \
	${CWERRS} ${CWARNS}

include ${TOP}make/actions.mk

all: $(addprefix ${BLDDIR}, libkmpthread.a) $(addprefix ${BLDDIR}, libruntime.a) $(addprefix ${BLDDIR}, libm.a)

KM_PTHREAD_OFILES = $(filter $(addprefix %,${KM_PTHREAD_OBJS}), ${OBJS})
OFILES = $(filter-out $(addprefix %,${KM_PTHREAD_OBJS}), ${OBJS})

$(addprefix ${BLDDIR}, libkmpthread.a): ${KM_PTHREAD_OFILES}
	@echo "Making " $@; rm -f $@; ld -r ${KM_PTHREAD_OFILES} -o $*.o; ${AR} crs $@ -o $*.o

$(addprefix ${BLDDIR}, libruntime.a): ${OFILES}
	@echo "Making " $@; rm -f $@; ${AR} crs $@ ${OFILES}

$(addprefix ${BLDDIR}, libm.a):
	@echo "Making " $@; rm -f $@; ${AR} crs $@

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
