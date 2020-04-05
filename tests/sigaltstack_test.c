#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "greatest/greatest.h"
#include "syscall.h"

sigjmp_buf jbuf;

stack_t ss;
struct sigaction sa;
char* bad = 0;
char good[0x1000];

void handler(int sig, siginfo_t* info, void* ucontext)
{
   void* sp = &sig;
   if (sig == SIGSEGV && ss.ss_sp <= sp && sp < ss.ss_sp + ss.ss_size) {
      bad = good;
      siglongjmp(jbuf, SIGSEGV);
      return;
   }
   abort();
}

TEST sas()
{
   if ((ss.ss_sp = malloc(SIGSTKSZ)) == NULL) {
      perror("malloc");
      exit(EXIT_FAILURE);
   }

   ss.ss_size = SIGSTKSZ;
   ss.ss_flags = 0;
   if (sigaltstack(&ss, NULL) == -1) {
      perror("sigaltstack");
      exit(EXIT_FAILURE);
   }

   sa.sa_flags = SA_ONSTACK;
   sa.sa_sigaction = handler; /* Address of a signal handler */
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGSEGV, &sa, NULL) == -1) {
      perror("sigaction");
      exit(EXIT_FAILURE);
   }
   int ret;
   if ((ret = sigsetjmp(jbuf, 1)) == 0) {
      strcpy((char*)bad, "writing to unmapped area");
      FAILm("Write successful and should be not");
   } else {
      ASSERT_EQm("Did not get expected signal", ret, SIGSEGV);
      printf("Handled SIGSEGV with altstack... continuing...\n");
   }
   PASS();
}

/* Inserts misc defintions */
GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args
   // greatest_set_verbosity(1);

   /* Tests can be run as suites, or directly. Lets run directly. */
   RUN_TEST(sas);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}