#include <pthread.h>
#include <sched.h>

int pthread_yield(void)
{
   return sched_yield();
}
