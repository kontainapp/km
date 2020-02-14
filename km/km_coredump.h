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

#endif