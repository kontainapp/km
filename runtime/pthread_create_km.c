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

_Noreturn void __pt_entry__(void)
{
   struct pthread* self = __pthread_self();

   self->result = (void*)self->start(self->start_arg);
   while (1) {
      __syscall1(SYS_exit, self->result);
   }
}

int __pthread_create(pthread_t* restrict res,
                     const pthread_attr_t* restrict ap,
                     void* (*entry)(void*),
                     void* restrict arg)
{
   unsigned long rc;

   a_inc(&__libc.threads_minus_1);
   /*
    * We need to make sure __pt_entry__ function gets into the executable if this function is there.
    * However running ld with --gc-sections removes __pt_entry__ because the only reference is from
    * inside km monitor, and of course ld is not aware of that. We generate the reference here but
    * putting __pt_entry__ address into the fifth argument, which is totally ignored by
    * pthread_create implementation in km.
    */
   if ((rc = __syscall5(HC_pthread_create, res, ap, entry, arg, __pt_entry__)) != 0) {
      a_dec(&__libc.threads_minus_1);
   }
   return -rc;
}

weak_alias(__pthread_create, pthread_create);
weak_alias(__pthread_exit, pthread_exit);
