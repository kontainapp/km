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

weak_alias(__pthread_exit, pthread_exit);
