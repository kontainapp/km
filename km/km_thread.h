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

#ifndef __KM_THREAD_H__
#define __KM_THREAD_H__

#include <pthread.h>

#include "km.h"
#include "chan/chan.h"

/*
 * thread related stuff
 */
#define VCPU_THREAD_CNT 1

/*
 * thread ids and channels to communicated to/from the threads
 * We separate channels for convenience and code simplification.
 *
 * Note: despite the appearance,it is really managing 1 vcpu only
 * TBD: multi-threads
 */
typedef struct km_threads {
   pthread_t main_thread;
   pthread_t gdbsrv_thread;
   chan_t *gdb_chan;
   pthread_t vcpu_thread[VCPU_THREAD_CNT];
   km_vcpu_t *vcpu[VCPU_THREAD_CNT];
   chan_t *vcpu_chan[VCPU_THREAD_CNT];
   } km_threads_t;

// TBD: add getter/setters and hide the struct
extern km_threads_t g_km_threads;
#endif /* __KM_THREAD_H__ */
