#include "libc.h"
#include "pthread_impl.h"

#include "km_hcalls.h"

_Noreturn void __pthread_exit(void* result)
{
   struct pthread* self = __pthread_self();

   self->result = result;
   while (1) {
      __syscall1(SYS_exit, self->result);
   }
}

_Noreturn void __pthread_entry__(void)
{
   struct pthread* self = __pthread_self();

   self->result = (void*)self->start(self->start_arg);
   while (1) {
      __syscall1(SYS_exit, self->result);
   }
}

int __pthread_create(pthread_t* restrict res,
                     const pthread_attr_t* restrict attrp,
                     void* (*entry)(void*),
                     void* restrict arg)
{
   unsigned long rc;

   a_inc(&__libc.threads_minus_1);
   if ((rc = __syscall5(HC_pthread, res, attrp, __pthread_entry__, entry, arg)) > -0x1000ul) {
      a_dec(&__libc.threads_minus_1);
      errno = -rc;
      return -1;
   }
   return rc;
}

weak_alias(__pthread_create, pthread_create);
weak_alias(__pthread_exit, pthread_exit);
