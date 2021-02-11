/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 */

#ifndef __KM_SID_PGID_H__
#define __KM_SID_PGID_H__

uint64_t km_setsid(km_vcpu_t* vcpu);
uint64_t km_getsid(km_vcpu_t* vcpu, pid_t pid);
uint64_t km_setpgid(km_vcpu_t* vcpu, pid_t pid, pid_t pgid);
uint64_t km_getpgid(km_vcpu_t* vcpu, pid_t pid);

#endif   // !defined(__KM_SID_PGID_H__)
