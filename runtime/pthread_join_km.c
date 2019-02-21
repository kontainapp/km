#include "pthread_impl.h"

int __pthread_join(pthread_t thread, void** retval)
{
   return -__syscall2(HC_pthread_join, thread, retval);
}

weak_alias(__pthread_join, pthread_join);
