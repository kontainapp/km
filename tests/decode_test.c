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
 * Tests for instruction decode.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#include "greatest/greatest.h"

#define PAGE_SIZE 4096

char goodbuf[PAGE_SIZE];
int datapage_size = PAGE_SIZE;
void* datapage_page = NULL;
siginfo_t datapage_siginfo;
struct sigaction old_sa = {};

void datapage_sigaction(int signo, siginfo_t* info, void* uc)
{
   if (mprotect(datapage_page, datapage_size, PROT_READ | PROT_WRITE) < 0) {
      perror("sigaction mprotect");
   } else {
      // Only set siginfo if mprotect succeeds.
      datapage_siginfo = *info;
   }
}

int setup()
{
   struct sigaction sa = {.sa_sigaction = datapage_sigaction, .sa_flags = SA_SIGINFO};

   if (sigaction(SIGSEGV, &sa, &old_sa) != 0) {
      return -1;
   }
   datapage_page = mmap(0, datapage_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (datapage_page == MAP_FAILED) {
      return -1;
   }
   memset(&datapage_siginfo, 0, sizeof(datapage_siginfo));
   return 0;
}

int teardown()
{
   if (munmap(datapage_page, datapage_size) != 0) {
      return -1;
   }
   if (sigaction(SIGSEGV, &old_sa, NULL) != 0) {
      return -1;
   }
   return 0;
}

void* failing_page()
{
   return (void*)(((uint64_t)datapage_siginfo.si_addr / PAGE_SIZE) * PAGE_SIZE);
}

TEST mode0_test()
{
   ASSERT_EQ(0, setup());

   asm volatile("mov %0, %%r10\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r10)"
                : /* No output */
                : "r"(datapage_page)
                : "%r10", "%rax");

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST mode1_test()
{
   ASSERT_EQ(0, setup());

   asm volatile("mov %0, %%r10\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, 100(%%r10)"
                : /* No output */
                : "r"(datapage_page)
                : "%r10", "%rax");

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST mode2_test()
{
   ASSERT_EQ(0, setup());

   asm volatile("mov %0, %%r10\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, 1000(%%r10)"
                : /* No output */
                : "r"(datapage_page)
                : "%r10", "%rax");

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST memset_test()
{
   ASSERT_EQ(0, setup());

   memset(datapage_page, 0, datapage_size);

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST copyin_test()
{
   ASSERT_EQ(0, setup());

   memcpy(goodbuf, datapage_page, PAGE_SIZE);

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST copyout_test()
{
   ASSERT_EQ(0, setup());

   memcpy(datapage_page, goodbuf, PAGE_SIZE);

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST mem_source_test()
{

   /*
    * Make sure we decode correctly when memory is the source.
    * Use RDX because it is also used by guest interrupt
    * handlers and this makes sure it gets restored correctly.
    */
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rdx\n\t"
                "mov (%%rdx), %%rax"
                : /* No output */
                : "r"(datapage_page)
                : "%rdx", "%rax");

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   // Same on bx
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rbx\n\t"
                "mov (%%rbx), %%rax"
                : /* No output */
                : "r"(datapage_page)
                : "%rbx", "%rax");

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   // same on cx
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rcx\n\t"
                "mov (%%rcx), %%rax"
                : /* No output */
                : "r"(datapage_page)
                : "%rcx", "%rax");

   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   PASS();
}

TEST mem_RSIaddress_test()
{
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rsi\n\t"
                "lodsw"
                : /* No output */
                : "r"(datapage_page)
                : "%rsi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST mem_RDIaddress_test()
{
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rdi\n\t"
                "stosw"
                : /* No output */
                : "r"(datapage_page)
                : "%rdi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());

   ASSERT_EQ(0, teardown());
   PASS();
}

TEST test_movsd()
{
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rsi\n\t"
                "mov %1, %%rdi\n\t"
                "movsd"
                : /* No output */
                : "r"(datapage_page), "r"(goodbuf)
                : "%rsi", "%rdi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rsi\n\t"
                "mov %1, %%rdi\n\t"
                "movsd"
                : /* No output */
                : "r"(goodbuf), "r"(datapage_page)
                : "%rsi", "%rdi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   PASS();
}

/*
 * Failing indirect access through all registers.
 */
TEST test_allreg()
{
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r8\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r8)"
                : /* No output */
                : "r"(datapage_page)
                : "%r8", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r9\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r9)"
                : /* No output */
                : "r"(datapage_page)
                : "%r9", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r10\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r10)"
                : /* No output */
                : "r"(datapage_page)
                : "%r10", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r11\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r11)"
                : /* No output */
                : "r"(datapage_page)
                : "%r11", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r12\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r12)"
                : /* No output */
                : "r"(datapage_page)
                : "%r12", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r13\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r13)"
                : /* No output */
                : "r"(datapage_page)
                : "%r13", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r14\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r14)"
                : /* No output */
                : "r"(datapage_page)
                : "%r14", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%r15\n\t"
                "xor %%rax, %%rax\n\t"
                "test %%rax, (%%r15)"
                : /* No output */
                : "r"(datapage_page)
                : "%r15", "%rax");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   PASS();
}


TEST mem_RSI_RDIaddress_test()
{
   // Compare RSI failing buf
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rsi\n\t"
                "mov %1, %%rdi\n\t"
                "cmpsw"
                : /* No output */
                : "r"(datapage_page), "r"(goodbuf)
                : "%rsi", "%rdi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());

   // Compare RDI failing buf
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rsi\n\t"
                "mov %1, %%rdi\n\t"
                "cmpsw"
                : /* No output */
                : "r"(goodbuf), "r"(datapage_page)
                : "%rsi", "%rdi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());
   PASS();
}

TEST test_2byte()
{
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rdi\n\t"
                "movdqu %%xmm0, (%%rdi)"
                : /* No output */
                : "r"(datapage_page)
                : "%rdi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());
   PASS();
}

TEST test_3byte()
{
   ASSERT_EQ(0, setup());
   asm volatile("mov %0, %%rdi\n\t"
                "phaddd (%%rdi), %%xmm1"
                : /* No output */
                : "r"(datapage_page)
                : "%rdi");
   ASSERT_EQ(SIGSEGV, datapage_siginfo.si_signo);
   ASSERT_EQ(datapage_page, failing_page());
   ASSERT_EQ(0, teardown());
   PASS();
}

TEST test_avx()
{
   asm volatile("vpxor %%xmm0, %%xmm0, %%xmm0" : : : "%xmm0");
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char* argv[])
{
   GREATEST_MAIN_BEGIN();
   greatest_set_verbosity(1);

   RUN_TEST(mode0_test);
   RUN_TEST(mode1_test);
   RUN_TEST(mode2_test);
   RUN_TEST(test_allreg);
   RUN_TEST(memset_test);
   RUN_TEST(copyin_test);
   RUN_TEST(copyout_test);
   RUN_TEST(mem_source_test);
   RUN_TEST(mem_RSIaddress_test);
   RUN_TEST(mem_RDIaddress_test);
   RUN_TEST(mem_RSI_RDIaddress_test);
   RUN_TEST(test_movsd);
   RUN_TEST(test_2byte);
   RUN_TEST(test_3byte);
   RUN_TEST(test_avx);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
