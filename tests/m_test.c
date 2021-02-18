#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>

void dump_mutex(pthread_mutex_t* mtxp)
{
   for (unsigned long* lp = (unsigned long*)mtxp; (char*)lp < (char*)(mtxp + 1); lp++) {
      printf("0x%lx ", *lp);
   }
   printf("\n");
}

int main(int argc, char** argv)
{
   pthread_mutex_t mtx = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
   pthread_mutex_t mtx1;

   pthread_mutexattr_t attr;
   pthread_mutexattr_init(&attr);
   pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
   pthread_mutex_init(&mtx1, &attr);
   pthread_mutexattr_destroy(&attr);

   dump_mutex(&mtx);
   dump_mutex(&mtx1);
}