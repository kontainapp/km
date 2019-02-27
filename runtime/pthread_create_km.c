#include "libc.h"
#include "pthread_impl.h"

_Noreturn void __pthread_exit(void* result)
{
   struct pthread* self = __pthread_self();

   self->result = result;
   while (1) {
      __syscall1(SYS_exit, self->result);
   }
}

extern _Noreturn void __start_c__(long arg);

int __pthread_create(pthread_t* restrict res,
                     const pthread_attr_t* restrict ap,
                     void* (*entry)(void*),
                     void* restrict arg)
{
   unsigned long rc;

   a_inc(&__libc.threads_minus_1);
   if ((rc = __syscall4(HC_pthread_create, res, ap, entry, arg)) != 0) {
      a_dec(&__libc.threads_minus_1);
   }
   return -rc;
}

weak_alias(__pthread_create, pthread_create);
weak_alias(__pthread_exit, pthread_exit);
