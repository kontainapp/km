/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

#ifndef __KM_SIGNAL_H__
#define __KM_SIGNAL_H__

#include <sys/signalfd.h>
#include "km.h"

extern void km_vcpu_unblock_signal(km_vcpu_t* vcpu, int signal);
extern int km_get_signalfd(int signal);
extern void km_wait_for_signal(int sig);
extern void km_reset_pending_signal(int signal);

#endif /* __KM_SIGNAL_H__ */