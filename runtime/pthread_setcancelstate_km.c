#include "pthread_impl.h"

int __pthread_setcancelstate(int new, int* old)
{
   if (new > 2U) {   // PTHREAD_CANCEL_MASKED
      return EINVAL;
   }
   struct pthread* self = __pthread_self();
   if (old) {
      *old = self->canceldisable;
   }
   self->canceldisable = new;
   sigset_t set;
   sigemptyset(&set);
   static const int s = SIGCANCEL - 1;
   // this is taken from sigaddset() which we can't use because it rejects SIGCANCEL
   set.__bits[s / 8 / sizeof *set.__bits] |= 1UL << (s & 8 * sizeof *set.__bits - 1);
   pthread_sigmask(new == PTHREAD_CANCEL_ENABLE ? SIG_UNBLOCK : SIG_BLOCK, &set, NULL);
   return 0;
}

weak_alias(__pthread_setcancelstate, pthread_setcancelstate);
