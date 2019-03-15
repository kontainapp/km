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

extern void km_wait_for_signal(int sig);
extern void km_block_signal(int signum);
typedef void (*sa_handler_t)(int);
extern void km_install_sighandler(int signum, sa_handler_t hander_func);
#endif /* __KM_SIGNAL_H__ */