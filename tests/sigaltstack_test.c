#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "greatest/greatest.h"
#include "syscall.h"

/*
 * Test sigaltstack call. We set sigaltstack and sigaction to use it for SIGSEGV. In the handler we
 * first check if address of local var matches the sigaltstack range, and then remedy the SIGSEGV.
 *
 * There are two scenarios - jumping from signal handler via siglongjmp (handler_lj() and sas_lj()),
 * and returning from handler via regular return (handler() and sas()). The two are checking that we
 * correctly track return from sigaltstack.
 *
 * Each scenario triggers SIGSEGV by writing into pointer called `bad'. In the first case it points
 * to 0, signal handler mallocs new memory and assigns bad to it. siglongjmp restarts execution at
 * the line above the assignment. In the second case bad point into allocated memory that is
 * mprotected to readonly, and signal handler changes protection, so returning from the handler
 * restarts the very instruction that caused SIGSEGV.
 */

#define PAGE_SIZE 4096

stack_t ss;
int* bad = 0;

sigjmp_buf jbuf;

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

void handler(int sig, siginfo_t* info, void* ucontext)
{
   void* sp = &sig;

   if (sig == SIGSEGV && ss.ss_sp <= sp && sp < ss.ss_sp + ss.ss_size) {
      if (greatest_get_verbosity() == 1) {
         printf("allowing writes on %p\n", bad);
      }
      mprotect(bad, PAGE_SIZE, PROT_WRITE | PROT_READ);
   } else {
      abort();
   }
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

   // make sure we do the mprotect on a dedicated page.
   void* ptr = malloc(PAGE_SIZE * 2);
   uintptr_t badpg = (uintptr_t)ptr;
   if (badpg % PAGE_SIZE != 0) {
      // Next page boundary
      badpg = (badpg + PAGE_SIZE) & ~(PAGE_SIZE - 1);
   }
   bad = (int*)badpg;

   mprotect(bad, PAGE_SIZE, PROT_READ);
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