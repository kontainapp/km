#include "libc.h"
#include "pthread_impl.h"
#include "stdio_impl.h"

_Noreturn void __pthread_exit(void* result)
{
   __pthread_tsd_run_dtors();
   __do_orphaned_stdio_locks();

   if (a_fetch_add(&libc.threads_minus_1, -1) == 0) {
      libc.threads_minus_1 = 0;
      exit((int)result);
   }
   while (1) {
      __syscall1(SYS_exit, result);
   }
}

extern _Noreturn void __start_c__(long arg);

static void init_file_lock(FILE* f)
{
   if (f != NULL && f->lock < 0) {
      f->lock = 0;
   }
}

int __pthread_create(pthread_t* restrict res,
                     const pthread_attr_t* restrict ap,
                     void* (*entry)(void*),
                     void* restrict arg)
{
   unsigned long rc;

   if (libc.threaded == 0) {
      /*
       * We get here only once in a payload life time, when (and if) main thread calls the very
       * first pthread_create, hence there is nobody to race with.
       * After we convert all files open to this ploint, the newly created ones are handled
       * correctly because we set libc.threaded.
       */
      libc.threaded = 1;
      for (FILE* f = *__ofl_lock(); f != NULL; f = f->next) {
         init_file_lock(f);
      }
      __ofl_unlock();
      init_file_lock(__stdin_used);
      init_file_lock(__stdout_used);
      init_file_lock(__stderr_used);
      if (__pthread_self()->tsd == NULL) {
         __pthread_self()->tsd = (void**)__pthread_tsd_main;
      }
   }

   a_inc(&__libc.threads_minus_1);
   if ((rc = __syscall4(HC_pthread_create, res, ap, entry, arg)) != 0) {
      a_dec(&__libc.threads_minus_1);
   }
   return -rc;
}

weak_alias(__pthread_create, pthread_create);
weak_alias(__pthread_exit, pthread_exit);
