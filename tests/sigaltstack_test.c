#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "greatest/greatest.h"
#include "syscall.h"

sigjmp_buf jbuf;

stack_t ss;
int* bad = 0;

void handler_lj(int sig, siginfo_t* info, void* ucontext)
{
   void* sp = &sig;

   if (sig == SIGSEGV && ss.ss_sp <= sp && sp < ss.ss_sp + ss.ss_size) {
      bad = malloc(1024);
      siglongjmp(jbuf, SIGSEGV);
   } else {
      abort();
   }
}

void handler(int sig, siginfo_t* info, void* ucontext)
{
   void* sp = &sig;

   if (sig == SIGSEGV && ss.ss_sp <= sp && sp < ss.ss_sp + ss.ss_size) {
      if (greatest_get_verbosity() == 1) {
         printf("allowing writes on %p\n", bad);
      }
      mprotect(bad, 4096, PROT_WRITE | PROT_READ);
   } else {
      abort();
   }
}

TEST sas_lj()
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

   struct sigaction sa;
   sa.sa_flags = SA_ONSTACK;
   sa.sa_sigaction = handler_lj;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGSEGV, &sa, NULL) == -1) {
      perror("sigaction");
      exit(EXIT_FAILURE);
   }
   sigsetjmp(jbuf, 0);
   if (greatest_get_verbosity() == 1) {
      printf("about to assign to %p\n", bad);
   }
   *bad = 17;
   ASSERT_EQ(17, *bad);

   stack_t lss;
   memset(&lss, 0, sizeof(lss));
   if (sigaltstack(NULL, &lss) == -1) {
      perror("sigaltstack");
      exit(EXIT_FAILURE);
   }
   if (greatest_get_verbosity() == 1) {
      printf("ss_flags = 0x%x\n", lss.ss_flags);
   }
   free(ss.ss_sp);
   free(bad);
   bad = 0;
   PASS();
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

   struct sigaction sa;
   sa.sa_flags = SA_ONSTACK;
   sa.sa_sigaction = handler;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGSEGV, &sa, NULL) == -1) {
      perror("sigaction");
      exit(EXIT_FAILURE);
   }
   bad = malloc(4096);
   mprotect(bad, 4096, PROT_READ);
   if (greatest_get_verbosity() == 1) {
      printf("about to assign to %p\n", bad);
   }
   *bad = 17;
   ASSERT_EQ(17, *bad);

   stack_t lss;
   memset(&lss, 0, sizeof(lss));
   if (sigaltstack(NULL, &lss) == -1) {
      perror("sigaltstack");
      exit(EXIT_FAILURE);
   }
   if (greatest_get_verbosity() == 1) {
      printf("ss_flags = 0x%x\n", lss.ss_flags);
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
   RUN_TEST(sas_lj);
   RUN_TEST(sas);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}