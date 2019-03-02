/* futex_demo.c

   Usage: futex_demo [nloops]
                    (Default: 5)

   Demonstrate the use of futexes in a program where parent and child
   use a pair of futexes located inside a shared anonymous mapping to
   synchronize access to a shared resource: the terminal. The two
   processes each write 'num-loops' messages to the terminal and employ
   a synchronization protocol that ensures that they alternate in
   writing messages.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <linux/futex.h>

#define errExit(msg)                                                                               \
   do {                                                                                            \
      perror(msg);                                                                                 \
      exit(EXIT_FAILURE);                                                                          \
   } while (0)

static int futex1;
long nloops;
long step = 1l << 23;
int var;

static int
futex(int* uaddr, int futex_op, int val, const struct timespec* timeout, int* uaddr2, int val3)
{
   return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, val3);
}

/* Acquire the futex pointed to by 'futexp': wait for its value to
   become 1, and then set the value to 0. */

static void fwait(int* futexp)
{
   int s;

   /* __sync_bool_compare_and_swap(ptr, oldval, newval) is a gcc
      built-in function.  It atomically performs the equivalent of:

          if (*ptr == oldval)
              *ptr = newval;

      It returns true if the test yielded true and *ptr was updated.
      The alternative here would be to employ the equivalent atomic
      machine-language instructions.  For further information, see
      the GCC Manual. */

   while (1) {
      /* Is the futex available? */

      if (__sync_bool_compare_and_swap(futexp, 1, 0))
         break; /* Yes */

      /* Futex is not available; wait */

      s = futex(futexp, FUTEX_WAIT, 0, NULL, NULL, 0);
      if (s == -1 && errno != EAGAIN)
         errExit("futex-FUTEX_WAIT");
   }
}

/* Release the futex pointed to by 'futexp': if the futex currently
   has the value 0, set its value to 1 and the wake any futex waiters,
   so that if the peer is blocked in fpost(), it can proceed. */

static void fpost(int* futexp)
{
   int s;

   /* __sync_bool_compare_and_swap() was described in comments above */

   if (__sync_bool_compare_and_swap(futexp, 0, 1)) {
      s = futex(futexp, FUTEX_WAKE, 1, NULL, NULL, 0);
      if (s == -1)
         errExit("futex-FUTEX_WAKE");
   }
}

void* child(void* arg)
{
   for (long j = 0; j < nloops; j++) {
      fwait(&futex1);
      ++var;
      if ((j & (step - 1)) == 0) {
         printf("Child  %ld - %d\n", j, var);
      }
      fpost(&futex1);
   }
   return NULL;
}

int main(int argc, char* argv[])
{
   pthread_t pt;

   nloops = (argc > 1) ? atoi(argv[1]) : 1l << 40;

   futex1 = 1;

   pthread_create(&pt, NULL, child, NULL);

   for (long j = 0; j < nloops; j++) {
      fwait(&futex1);
      --var;
      if ((j & (step - 1)) == 0) {
         printf("Parent %ld - %d\n", j, var);
      }
      fpost(&futex1);
   }
}
