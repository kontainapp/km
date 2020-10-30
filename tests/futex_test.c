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
#include <assert.h>
#include <err.h>
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

#include "km_hcalls.h"

#define errExit(msg) err(1, msg)

int futex1, futex2;

int do_snapshot;
int verbose;

long nloops = 1000;
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

   while (1) {
      /* Is the futex available? */

      int one = 1;
      if (__atomic_compare_exchange_n(futexp, &one, 0, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
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
   int zero = 0;

   if (__atomic_compare_exchange_n(futexp, &zero, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
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
      assert(var == 0);
      usleep(70);
      if (verbose) {
         printf("Child  %ld - %d\n", j, var);
      }
      fpost(&futex2);
   }
   return NULL;
}

void* parent(void* arg)
{
   for (long j = 0; j < nloops; j++) {
      fwait(&futex2);
      --var;
      assert(var == -1);
      usleep(30);
      if (verbose) {
         printf("Parent %ld - %d\n", j, var);
      }
      fpost(&futex1);
   }
   return NULL;
}

int main(int argc, char* argv[])
{
   int c;

   while ((c = getopt(argc, argv, "sl:v")) != -1) {
      switch (c) {
         case 's':
            do_snapshot = 1;
            break;

         case 'v':
            verbose = 1;
            break;

         case 'l':
            nloops = atoi(optarg);
            break;

         default:
            errx(1,
                 "Usage: %s -s -v -l <loops>\n"
                 "\t-s do snapshot\n"
                 "\t-v verbose\n"
                 "\t-l <loops> loop that many times, default 1000\n",
                 argv[0]);
            break;
      }
   }

   pthread_t pt1, pt2;

   futex1 = 0;   // not avalable
   futex2 = 1;   // avalable

   pthread_create(&pt1, NULL, child, NULL);
   pthread_create(&pt2, NULL, parent, NULL);

   usleep(10000);   // let the threads start

   if (do_snapshot != 0) {
      km_hc_args_t snapshotargs = {};
      km_hcall(HC_snapshot, &snapshotargs);
   }
   pthread_join(pt2, NULL);
   pthread_join(pt1, NULL);
}
