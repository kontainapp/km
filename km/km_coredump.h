/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#ifndef KM_COREDUMP_H_
#define KM_COREDUMP_H_

#include "km.h"
#include "x86_cpu.h"

// Core dump guest.
void km_dump_core(km_vcpu_t* vcpu, x86_interrupt_frame_t* iframe);

void km_set_coredump_path(char* path);
char* km_get_coredump_path();
void km_core_write_elf_header(int fd, int phnum);
int km_core_write_notes(km_vcpu_t* vcpu, int fd, off_t offset, char* buf, size_t size);
void km_core_write_load_header(int fd, off_t offset, km_gva_t base, size_t size, int flags);
void km_core_write(int fd, void* buffer, size_t length);
#endif