/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication of
 * such source code. Disclosure of this source code or any related proprietary
 * information is strictly prohibited without the express written permission of
 * Kontain Inc.
 *
 * Simplifued implementation of __cxa_thread_atexit_impl() and __call_tls_dtors()
 */
#include <errno.h>
#include <malloc.h>
#include <stddef.h>

typedef void (*km_dtor_func_t)(void*);

typedef struct km_dtor_list {
   km_dtor_func_t dtor_func;
   void* obj_to_d;
   struct km_dtor_list* next;
} km_dtor_list_t;

static __thread km_dtor_list_t* tls_dtor_list;

/*
 * Register dtor for thread_local object. This is called from C++ generated code through
 * __cxa_thread_atexit() wrapper.
 * Since we are statically linked we have no use of dso_symbol, and overall locking isn't needed as
 * this is accessed only by the thread it belomgs to.
 */
int __cxa_thread_atexit_impl(km_dtor_func_t dtor_func,
                             void* obj_to_d,
                             __attribute__((unused)) void* dso_symbol)
{
   km_dtor_list_t* new;

   if ((new = malloc(sizeof(km_dtor_list_t))) == NULL) {
      return ENOMEM;
   }
   new->dtor_func = dtor_func;
   new->obj_to_d = obj_to_d;
   new->next = tls_dtor_list;
   tls_dtor_list = new;
   return 0;
}

void __run_tls_dtors(void)
{
   while (tls_dtor_list != NULL) {
      km_dtor_list_t* tmp;

      tls_dtor_list->dtor_func(tls_dtor_list->obj_to_d);
      tmp = tls_dtor_list;
      tls_dtor_list = tls_dtor_list->next;
      free(tmp);
   }
}