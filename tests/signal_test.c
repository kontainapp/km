/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Simple test for signal system call.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "greatest/greatest.h"
#include "syscall.h"
#include "test_common_functions.h"

int signal_seen = 0;
void* stack_addr = NULL;

void signal_handler(int signal)
{
   asm volatile("movq %%rsp, %0" : "=r"(stack_addr));
   signal_seen++;
}

/*
 * Ensure that a simple signal is received.
 */
TEST test_simple_signal()
{
   signal_seen = 0;
   signal(SIGTERM, signal_handler);
   kill(0, SIGTERM);
   ASSERT_EQ(1, signal_seen);
   // check stack alignment.
   ASSERT_NEQ(0, (uintptr_t)stack_addr);
   ASSERT_EQ(0, ((uintptr_t)stack_addr) % 16);
   signal(SIGTERM, SIG_DFL);
   PASS();
}

/*
 * multiple signals masked out and re-enabled.
 */
TEST test_sigpending()
{
   sigset_t ss;
   sigset_t pending;

   signal_seen = 0;
   signal(SIGUSR1, signal_handler);
   signal(SIGUSR2, signal_handler);
   sigemptyset(&ss);
   sigaddset(&ss, SIGUSR1);
   sigaddset(&ss, SIGUSR2);
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &ss, NULL));
   kill(0, SIGUSR1);
   kill(0, SIGUSR2);
   ASSERT_EQ(0, sigpending(&pending));
   ASSERT_NEQ(0, sigismember(&pending, SIGUSR1));
   ASSERT_NEQ(0, sigismember(&pending, SIGUSR2));
   ASSERT_EQ(0, signal_seen);
   ASSERT_EQ(0, sigprocmask(SIG_UNBLOCK, &ss, NULL));
   ASSERT_EQ(2, signal_seen);
   signal(SIGUSR1, SIG_DFL);
   signal(SIGUSR2, SIG_DFL);

   // test bad sigsetsize
   ASSERT_EQ(-1, syscall(SYS_rt_sigpending, &pending, 4));
   ASSERT_EQ(EINVAL, errno);
   // test bad pointer
   ASSERT_EQ(-1, syscall(SYS_rt_sigpending, NULL, 4));
   ASSERT_EQ(EFAULT, errno);
   PASS();
}

/*
 * RT signals are queued.
 */
TEST test_sigqueued()
{
   sigset_t ss;
   sigset_t pending;

   signal_seen = 0;
   signal(SIGRTMIN, signal_handler);
   sigemptyset(&ss);
   sigaddset(&ss, SIGRTMIN);
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &ss, NULL));
   kill(0, SIGRTMIN);
   kill(0, SIGRTMIN);
   ASSERT_EQ(0, sigpending(&pending));
   ASSERT_NEQ(0, sigismember(&pending, SIGRTMIN));
   ASSERT_EQ(0, signal_seen);
   ASSERT_EQ(0, sigprocmask(SIG_UNBLOCK, &ss, NULL));
   ASSERT_EQ(2, signal_seen);
   signal(SIGRTMIN, SIG_DFL);
   PASS();
}

/*
 * 'old' signals (< SIGRTMIN) are consolodated, not queued.
 */
TEST test_sigconsolodated()
{
   sigset_t ss;
   sigset_t pending;

   signal_seen = 0;
   signal(SIGUSR1, signal_handler);
   sigemptyset(&ss);
   sigaddset(&ss, SIGUSR1);
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &ss, NULL));
   kill(0, SIGUSR1);
   kill(0, SIGUSR1);
   ASSERT_EQ(0, sigpending(&pending));
   ASSERT_NEQ(0, sigismember(&pending, SIGUSR1));
   ASSERT_EQ(0, signal_seen);
   ASSERT_EQ(0, sigprocmask(SIG_UNBLOCK, &ss, NULL));
   ASSERT_EQ(1, signal_seen);
   signal(SIGUSR1, SIG_DFL);
   PASS();
}

void first_signal_handler(int signo)
{
   kill(0, SIGUSR2);
}

TEST test_nested_signal()
{
   signal_seen = 0;
   signal(SIGUSR1, first_signal_handler);
   signal(SIGUSR2, signal_handler);

   ASSERT_EQ(0, signal_seen);
   kill(0, SIGUSR1);
   ASSERT_EQ(1, signal_seen);

   signal(SIGUSR1, SIG_DFL);
   signal(SIGUSR2, SIG_DFL);
   PASS();
}

TEST test_sigprocmask()
{
   sigset_t set;
   sigset_t oldset;

   sigemptyset(&set);
   sigaddset(&set, SIGUSR2);

   // Test success paths
   ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &set, &oldset));
   ASSERT_EQ(0, sigprocmask(SIG_SETMASK, &oldset, NULL));
   ASSERT_EQ(0, sigprocmask(SIG_SETMASK, NULL, &oldset));

   // test bogus 'how' to sigprocmask
   sigemptyset(&set);
   sigaddset(&set, SIGUSR2);
   ASSERT_EQ(-1, sigprocmask(57, &set, &oldset));
   ASSERT_EQ(EINVAL, errno);

   // test bad sigsetsize to sigprocmask.
   ASSERT_EQ(-1, syscall(SYS_rt_sigprocmask, SIG_BLOCK, &set, &oldset, 4));
   ASSERT_EQ(EINVAL, errno);

   PASS();
}

TEST test_sigaction()
{
   struct sigaction act;
   struct sigaction oldact;

   // get old action
   act.sa_handler = SIG_IGN;
   ASSERT_EQ(0, sigaction(SIGUSR1, NULL, &oldact));
   // Set new action
   ASSERT_EQ(0, sigaction(SIGUSR1, &act, &oldact));
   // restore old action
   ASSERT_EQ(0, sigaction(SIGUSR1, &oldact, NULL));

   // bad signal values.
   ASSERT_EQ(-1, syscall(SYS_rt_sigaction, 0, &oldact, NULL, 8));
   ASSERT_EQ(EINVAL, errno);
   ASSERT_EQ(-1, syscall(SYS_rt_sigaction, SIGRTMAX + 1, &oldact, NULL, 8));
   ASSERT_EQ(EINVAL, errno);

   // bad sigsetsize
   ASSERT_EQ(-1, syscall(SYS_rt_sigaction, 0, &oldact, NULL, 4));
   ASSERT_EQ(EINVAL, errno);

   PASS();
}

TEST test_kill()
{
   // Get an invalid pid.
   pid_t pid_max = get_pid_max();
   ASSERT_NEQ(-1, pid_max);

   // Use an invalid pid to provoke ESRCH error.
   ASSERT_EQ(-1, kill(pid_max, SIGUSR1));
   ASSERT_EQ(ESRCH, errno);

   // test bad signal numbers
   ASSERT_EQ(-1, kill(0, 0));
   ASSERT_EQ(EINVAL, errno);
   ASSERT_EQ(-1, kill(0, SIGRTMAX + 1));
   ASSERT_EQ(EINVAL, errno);
   PASS();
}

TEST test_tkill()
{
   // too small for KM vcpu
   ASSERT_EQ(-1, syscall(SYS_tkill, -1, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);
   // too large for KM vcpu
   ASSERT_EQ(-1, syscall(SYS_tkill, 1024, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);
   // unused KM vcpu. We are single threaded, tid is 1, so we use 2
   ASSERT_EQ(-1, syscall(SYS_tkill, 2, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);

   // sanity check for tgkill - same 'unused KM vcpu' as for tkill.
   // group id (100) is supposed to be ignored, we will check in in bats
   ASSERT_EQ(-1, syscall(SYS_tgkill, 100, 2, SIGUSR1));
   ASSERT_EQ(EINVAL, errno);

   // bad signal numbers
   ASSERT_EQ(-1, syscall(SYS_tkill, 0, 0));
   ASSERT_EQ(EINVAL, errno);
   ASSERT_EQ(-1, syscall(SYS_tkill, 0, SIGRTMAX + 1));
   ASSERT_EQ(EINVAL, errno);
   PASS();
}

sigset_t expmask;
int sigmask_pass = 0;
void sigact(int signo, siginfo_t* info, void* context)
{
   sigset_t mset;
   if (sigprocmask(SIG_SETMASK, NULL, &mset) < 0) {
      return;
   }
   if (*(uint64_t*)&expmask == *(uint64_t*)&mset) {
      sigmask_pass = 1;
   }
}

// Test sigmask manipulation
TEST test_sigmask()
{
   sigset_t pset;
   sigset_t aset;
   struct sigaction newact;
   struct sigaction oldact;

   sigemptyset(&pset);
   sigemptyset(&aset);

   // Get sigmask as it exists
   ASSERT_EQ(0, sigprocmask(SIG_SETMASK, NULL, &pset));

   sigfillset(&expmask);
   newact.sa_sigaction = sigact;
   newact.sa_mask = expmask;
   newact.sa_flags = SA_SIGINFO;
   ASSERT_EQ(0, sigaction(SIGUSR1, &newact, &oldact));

   kill(0, SIGUSR1);
   ASSERT_EQ(1, sigmask_pass);

   ASSERT_EQ(0, sigprocmask(SIG_SETMASK, NULL, &aset));
   ASSERT_EQ(*(uint64_t*)&pset, *(uint64_t*)&pset);
   PASS();
}

/*
 * Emulate OpenJDK safepoint algorithm.
 *
 * OpenJDK uses a page protection mechanism to implement global pause
 * for JIT generated code. The idea is maintain a page that is normally
 * readable but is set PROT_NONE when a global pause is needed. Threads
 * access the page when they get to 'safepoints'. The PROT_NONE generates
 * a SIGSEGV and the pause happens in the context of the SIGSEGV handler.
 * This signal handler uses info->si_addr to determine whether this is a
 * safepoint or a real error.
 */
int safepoint_size = 4096;
void* safepoint_page = NULL;
siginfo_t safepoint_siginfo;
void safepoint_sigaction(int signo, siginfo_t* info, void* uc)
{
   safepoint_siginfo = *info;
   mprotect(safepoint_page, safepoint_size, PROT_READ);
}

TEST test_safepoint()
{
   struct sigaction sa = {.sa_sigaction = safepoint_sigaction, .sa_flags = SA_SIGINFO};
   struct sigaction old_sa = {};

   ASSERT_EQ(0, sigaction(SIGSEGV, &sa, &old_sa));
   safepoint_page = mmap(0, safepoint_size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
   ASSERT_NEQ(MAP_FAILED, safepoint_page);

   asm volatile("mov %0, %%r10\n\t"
                "test %%rax, (%%r10)"
                : /* No output */
                : "r"(safepoint_page)
                : "%r10");

   ASSERT_EQ(safepoint_page, safepoint_siginfo.si_addr);
   ASSERT_EQ(0, munmap(safepoint_page, safepoint_size));
   ASSERT_EQ(0, sigaction(SIGSEGV, &old_sa, NULL));
   PASS();
}

void* tmain(void* arg)
{
   static void* sav;
   asm volatile("movq %%rsp, %0" : "=r"(stack_addr));
   return sav;
}

TEST test_thread_stack_alignment()
{
   pthread_t t;
   void* rval;
   ASSERT_EQ(0, pthread_create(&t, NULL, tmain, NULL));
   ASSERT_EQ(0, pthread_join(t, &rval));
   ASSERT_EQ(0, ((uintptr_t)rval) % 16);
   PASS();
}

void floating_point_sigaction(int signo, siginfo_t* info, void* uc)
{
   uint64_t two = 2;
   asm volatile("movq %0, %%xmm0"
                : /* No output */
                : "r"(two)
                : "%xmm0");
}

TEST test_floating_point_restore()
{
   struct sigaction sa = {.sa_sigaction = floating_point_sigaction, .sa_flags = SA_SIGINFO};
   struct sigaction old_sa = {};

   ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));
   uint64_t one = 1;
   // Set XMM0
   asm volatile("movq %0, %%xmm0"
                : /* No output */
                : "r"(one)
                : "%xmm0");

   // signal handler clobbers XMM0
   kill(0, SIGUSR1);

   // ensure XMM0 is restored after signal.
   uint64_t val = -1;
   asm volatile("movq %%xmm0, %0"
                : "=r"(val)
                : /* No input */
                :);
   ASSERT_EQ(one, val);
   ASSERT_EQ(0, sigaction(SIGUSR1, &old_sa, NULL));
   PASS();
}

int test_no_restorer_sigaction_called = 0;
void test_no_restorer_sigaction(int signo, siginfo_t* info, void* uc)
{
   test_no_restorer_sigaction_called = 1;
}

TEST test_no_restorer()
{
   struct sigaction sa = {.sa_sigaction = test_no_restorer_sigaction, .sa_flags = SA_SIGINFO};
   struct sigaction old_sa = {};

   /*
    * library call for sigaction inserts a SA_RESTORER. Use direct syscall instead.
    */
   ASSERT_EQ(0, syscall(SYS_rt_sigaction, SIGUSR1, &sa, &old_sa, 8));

   kill(0, SIGUSR1);

   ASSERT_EQ(1, test_no_restorer_sigaction_called);

   ASSERT_EQ(0, sigaction(SIGUSR1, &old_sa, NULL));
   PASS();
}

int test_sa_resethand_sigaction_called = 0;
void test_sa_resethandestorer_sigaction(int signo, siginfo_t* info, void* uc)
{
   test_sa_resethand_sigaction_called = 1;
}

TEST test_sa_resethand()
{
   struct sigaction sa = {.sa_sigaction = test_sa_resethandestorer_sigaction,
                          .sa_flags = SA_SIGINFO | SA_RESETHAND};
   struct sigaction old_sa = {};

   ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));
   ASSERT_EQ(0, kill(getpid(), SIGUSR1));
   ASSERT_EQ(1, test_sa_resethand_sigaction_called);
   ASSERT_EQ(0, sigaction(SIGUSR1, NULL, &old_sa));
   ASSERT_EQ((void (*)(int, siginfo_t*, void*))SIG_DFL, old_sa.sa_sigaction);
   ASSERT_EQ(0, old_sa.sa_flags & SA_RESETHAND);
   PASS();
}

TEST test_sigttou()
{
   int tty = open("/dev/tty", O_RDWR | O_CLOEXEC, 0);
   if (tty < 0) {
      // No tty's so we can't test this.
      fprintf(stderr, "Can't open /dev/tty, %s, skipping this test\n", strerror(errno));
      SKIP();
   }
   pid_t child = fork();
   if (child != 0) {
      // we are the parent
      ASSERT_NEQ(child, -1);

      close(tty);

      // wait for the child to exit.
      int wstatus;
      pid_t reaped_pid = wait4(child, &wstatus, 0, NULL);
      fprintf(stderr,
              "parent: reaped_pid %d, errno %d, expected pid %d, wstatus 0x%x\n",
              reaped_pid,
              errno,
              child,
              wstatus);
      ASSERT_EQ(reaped_pid, child);
      ASSERT_NEQ(WIFEXITED(wstatus), 0);
      ASSERT_EQ(WEXITSTATUS(wstatus), 0);
      PASS();
   } else {
      /*
       * we are the child.  We can't use the "greatest" primitives in the child.
       * We just abort() on error and the parent sees the child died and that will
       * fail the test.
       * The child process must block the SIGTTOU signal before calling tcsetpgrp()
       * (and km must tell the kernel this signal is blocked) to prevent the child
       * process from being stopped as a result of making the tcsetpgrp() call.
       * This is a basic smoke test for problems we were seeing when starting a shell
       * in a container.  Any command run from the shell would be put into background.
       */

      // Child is in its own process group.
      int rc = setpgid(0, 0);
      fprintf(stderr, "child: setpgid returned rc %d, errno %d\n", rc, errno);
      assert(rc == 0);

      // Block SIGTTOU
      void* rcp = signal(SIGTTOU, SIG_IGN);
      fprintf(stderr, "child: signal returned rcp %p, errno %d\n", rcp, errno);
      assert(rcp != SIG_ERR);

      // Assign the tty to our process group
      rc = tcsetpgrp(tty, getpid());
      fprintf(stderr, "child: tcsetpgrp rc %d, errno %d\n", rc, errno);
      assert(rc == 0);

      close(tty);

      // success
      exit(0);
   }
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_simple_signal);
   RUN_TEST(test_sigpending);
   RUN_TEST(test_sigqueued);
   RUN_TEST(test_sigconsolodated);
   RUN_TEST(test_nested_signal);
   RUN_TEST(test_sigprocmask);
   RUN_TEST(test_sigaction);
   RUN_TEST(test_kill);
   RUN_TEST(test_tkill);
   RUN_TEST(test_sigmask);
   RUN_TEST(test_safepoint);
   RUN_TEST(test_thread_stack_alignment);
   RUN_TEST(test_floating_point_restore);
   RUN_TEST(test_no_restorer);
   RUN_TEST(test_sa_resethand);
   RUN_TEST(test_sigttou);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
