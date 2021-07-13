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
 * Start a lot of threads
 * wait for sync points so context switch is guaranteed between setting and verifying registers
 *
 * stating a lots of thread with guaranteed context switches,
 * and validates that registeres are properly saved and restored
 *
 * signal test
 * verifies state is saved and restored across signal handler
 */

#define _GNU_SOURCE
#include <cpuid.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "greatest/greatest.h"

#define XSTATE_ITER_COUNT (16)

#define MAX_THREADS (32)

#define MAIN_PATTERN (0xF00D)
#define SIGNAL_PATTERN (0xDEAD)

typedef struct {
   int index;
   int failed_count;
   pthread_t thread_id;
   void* thread_exit_value;
} child_t;

typedef struct {
   pthread_mutex_t cv_mutex;
   pthread_cond_t cv;
   atomic_uint thread_started_count;
   atomic_uint thread_set_count;
   atomic_uint thread_verify_count;
   u_int64_t features;
   child_t child[MAX_THREADS];
} test_t;

test_t test;

#define MMX_REG_COUNT (8)
#define XMM_REG_COUNT (16)

#define SSE_TEST_REG_START (0)
#define SSE_TEST_REG_COUNT (4)
#define AVX_TEST_REG_START (4)
#define AVX_TEST_REG_COUNT (2)
#define ZMM_HI256_TEST_REG_COUNT (4)
#define HI16_ZMM_TEST_REG_COUNT (16)

typedef struct {
   u_int64_t d0;
   u_int64_t d1;
} xmm_t;

typedef struct {
   u_int64_t d0;
   u_int64_t d1;
   u_int64_t d2;
   u_int64_t d3;
} ymm_t;

typedef struct {
   u_int64_t d0;
   u_int64_t d1;
   u_int64_t d2;
   u_int64_t d3;
   u_int64_t d4;
   u_int64_t d5;
   u_int64_t d6;
   u_int64_t d7;
} zmm_t;

/*
 * intel SDM volume 3A section 2.6 XCR0 bit definition
 */
#define X87 (0x1)
#define SSE (0x2)
#define AVX (0x4)
#define BNDREG (0x8)
#define BNDCSR (0x10)
#define OPMASK (0x20)
#define ZMM_HI256 (0x40)
#define HI16_ZMM (0x80)
/* bit 8 unused */
#define PKRU (0x200)

void get_xtended_features()
{
   u_int32_t eax, edx;
   u_int32_t ecx = 0;

   asm volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(ecx));
   test.features = ((u_int64_t)edx << 32) | eax;
}

u_int64_t get_value(u_int32_t index, u_int32_t value)
{
   return ((u_int64_t)index << 32) | value;
}

void set_xtended_registers(child_t* cptr)
{
   if (test.features & X87) {
      /* Floating point and MMX */
      u_int64_t mmx_values[MMX_REG_COUNT];

      for (int index = 0; index < MMX_REG_COUNT; index++) {
         mmx_values[index] = get_value(cptr->index, index);
      }
      __asm__ volatile("movq (%0), %%mm0\n"
                       "movq 0x8(%0), %%mm1\n"
                       "movq 0x10(%0), %%mm2\n"
                       "movq 0x18(%0), %%mm3\n"
                       "movq 0x20(%0), %%mm4\n"
                       "movq 0x28(%0), %%mm5\n"
                       "movq 0x30(%0), %%mm6\n"
                       "movq 0x38(%0), %%mm7\n"
                       :
                       : "r"(mmx_values));
   } else {
      printf("info: FPU/MMX save/restore is not supported, no need to test\n");
   }

   if (test.features & SSE) {
      /* SSE (XMM registers) */
      xmm_t xmm_values[SSE_TEST_REG_COUNT] __attribute__((aligned(16)));
      for (int index = 0; index < SSE_TEST_REG_COUNT; index++) {
         xmm_values[index].d1 = xmm_values[index].d0 = get_value(cptr->index, index);
      }
      __asm__ volatile("movdqa (%0), %%xmm0\n"
                       "movdqa 0x10(%0), %%xmm1\n"
                       "movdqa 0x20(%0), %%xmm2\n"
                       "movdqa 0x30(%0), %%xmm3\n"
                       :
                       : "r"(xmm_values));
   } else {
      printf("info: SSE save/restore is not supported, no need to test\n");
   }

   if (test.features & AVX) {
      /* AVX (XMM registers) */
      xmm_t xmm_values[AVX_TEST_REG_COUNT] __attribute__((aligned(16)));
      for (int index = 0; index < AVX_TEST_REG_COUNT; index++) {
         xmm_values[index].d1 = xmm_values[index].d0 = get_value(cptr->index, index);
      }
      __asm__ volatile("vmovdqa (%0), %%xmm4\n"
                       "vmovdqa 0x10(%0), %%xmm5\n"
                       :
                       : "r"(xmm_values));

      /* AVX (YMM registers) */
      ymm_t ymm_values[AVX_TEST_REG_COUNT] __attribute__((aligned(32)));
      for (int index = 0; index < AVX_TEST_REG_COUNT; index++) {
         ymm_values[index].d3 = ymm_values[index].d2 =
         ymm_values[index].d1 = ymm_values[index].d0 = get_value(cptr->index, index);
      }
      __asm__ volatile("vmovdqa (%0), %%ymm6\n"
                       "vmovdqa 0x20(%0), %%ymm7\n"
                       :
                       : "r"(ymm_values));
   } else {
      printf("info: AVX save/restore is not supported, no need to test\n");
   }

   if (test.features & ZMM_HI256) {
      /* AVX (ZMM0-ZMM15 registers) */
      zmm_t zmm_values[ZMM_HI256_TEST_REG_COUNT] __attribute__((aligned(64)));
      for (int index = 0; index < ZMM_HI256_TEST_REG_COUNT; index++) {
         zmm_values[index].d7 = zmm_values[index].d6 =
         zmm_values[index].d5 = zmm_values[index].d4 =
         zmm_values[index].d3 = zmm_values[index].d2 =
         zmm_values[index].d1 = zmm_values[index].d0 = get_value(cptr->index, index);
      }
      __asm__ volatile("vmovdqa32 (%0), %%zmm8\n"
                       "vmovdqa32 0x40(%0), %%zmm9\n"
                       "vmovdqa64 0x80(%0), %%zmm10\n"
                       "vmovdqa64 0xC0(%0), %%zmm11\n"
                       :
                       : "r"(zmm_values));
   }

   if (test.features & HI16_ZMM) {
      /* AVX512 (ZMM16-ZMM31 registers) */
      zmm_t zmm_values[HI16_ZMM_TEST_REG_COUNT] __attribute__((aligned(64)));
      for (int index = 0; index < HI16_ZMM_TEST_REG_COUNT; index++) {
         zmm_values[index].d7 = zmm_values[index].d6 =
         zmm_values[index].d5 = zmm_values[index].d4 =
         zmm_values[index].d3 = zmm_values[index].d2 =
         zmm_values[index].d1 = zmm_values[index].d0 = get_value(cptr->index, index);
      }
      __asm__ volatile("vmovdqa32 (%0), %%zmm16\n"
                       "vmovdqa32 0x40(%0), %%zmm17\n"
                       "vmovdqa32 0x80(%0), %%zmm18\n"
                       "vmovdqa32 0xC0(%0), %%zmm19\n"
                       "vmovdqa32 0x100(%0), %%zmm20\n"
                       "vmovdqa32 0x140(%0), %%zmm21\n"
                       "vmovdqa32 0x180(%0), %%zmm22\n"
                       "vmovdqa32 0x1C0(%0), %%zmm23\n"
                       "vmovdqa64 0x200(%0), %%zmm24\n"
                       "vmovdqa64 0x240(%0), %%zmm25\n"
                       "vmovdqa64 0x280(%0), %%zmm26\n"
                       "vmovdqa64 0x2C0(%0), %%zmm27\n"
                       "vmovdqa64 0x300(%0), %%zmm28\n"
                       "vmovdqa64 0x340(%0), %%zmm29\n"
                       "vmovdqa64 0x380(%0), %%zmm30\n"
                       "vmovdqa64 0x3C0(%0), %%zmm31\n"
                       :
                       : "r"(zmm_values));
   }

   /* TODO add rest of the features when time permits */
}

void verify_xtended_registers(child_t* cptr)
{
   if (test.features & X87) {
      u_int64_t mmx_values[MMX_REG_COUNT];

      __asm__ volatile("movq %%mm0, (%0)\n"
                       "movq %%mm1, 0x8(%0)\n"
                       "movq %%mm2, 0x10(%0)\n"
                       "movq %%mm3, 0x18(%0)\n"
                       "movq %%mm4, 0x20(%0)\n"
                       "movq %%mm5, 0x28(%0)\n"
                       "movq %%mm6, 0x30(%0)\n"
                       "movq %%mm7, 0x38(%0)\n"
                       :
                       : "r"(mmx_values));
      for (int index = 0; index < MMX_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if (mmx_values[index] != expected) {
            cptr->failed_count++;
         }
      }
   }

   if (test.features & SSE) {
      /* SSE (XMM registers) */
      xmm_t xmm_values[SSE_TEST_REG_COUNT] __attribute__((aligned(16)));
      __asm__ volatile("movdqa %%xmm0, (%0)\n"
                       "movdqa %%xmm1, 0x10(%0)\n"
                       "movdqa %%xmm2, 0x20(%0)\n"
                       "movdqa %%xmm3, 0x30(%0)\n"
                       :
                       : "r"(xmm_values));
      for (int index = 0; index < SSE_TEST_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if ((xmm_values[index].d1 != expected) || (xmm_values[index].d0 != expected)) {
            cptr->failed_count++;
         }
      }
   }

   if (test.features & AVX) {
      /* AVX (XMM registers) */
      xmm_t xmm_values[AVX_TEST_REG_COUNT] __attribute__((aligned(16)));
      __asm__ volatile("vmovdqa %%xmm4, (%0)\n"
                       "vmovdqa %%xmm5, 0x10(%0)\n"
                       :
                       : "r"(xmm_values));
      for (int index = 0; index < AVX_TEST_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if ((xmm_values[index].d1 != expected) || (xmm_values[index].d0 != expected)) {
            cptr->failed_count++;
         }
      }

      /* AVX (YMM registers) */
      ymm_t ymm_values[AVX_TEST_REG_COUNT] __attribute__((aligned(32)));
      __asm__ volatile("vmovdqa %%ymm6, (%0)\n"
                       "vmovdqa %%ymm7, 0x20(%0)\n"
                       :
                       : "r"(ymm_values));
      for (int index = 0; index < AVX_TEST_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if ((ymm_values[index].d3 != expected) || (ymm_values[index].d2 != expected) ||
            (ymm_values[index].d1 != expected) || (ymm_values[index].d0 != expected)) {
            cptr->failed_count++;
         }
      }
   }

   if (test.features & ZMM_HI256) {
      /* AVX (ZMM0-ZMM15 registers) */
      zmm_t zmm_values[ZMM_HI256_TEST_REG_COUNT] __attribute__((aligned(64)));
      __asm__ volatile("vmovdqa32 %%zmm8, (%0)\n"
                       "vmovdqa32 %%zmm9, 0x40(%0)\n"
                       "vmovdqa64 %%zmm10, 0x80(%0)\n"
                       "vmovdqa64 %%zmm11, 0xC0(%0)\n"
                       :
                       : "r"(zmm_values));
      for (int index = 0; index < ZMM_HI256_TEST_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if ((zmm_values[index].d7 != expected) || (zmm_values[index].d6 != expected) ||
            (zmm_values[index].d5 != expected) || (zmm_values[index].d4 != expected) ||
            (zmm_values[index].d3 != expected) || (zmm_values[index].d2 != expected) ||
            (zmm_values[index].d1 != expected) || (zmm_values[index].d0 != expected)) {
            cptr->failed_count++;
         }
      }
   }

   if (test.features & HI16_ZMM) {
      /* AVX512 (ZMM16-ZMM31 registers) */
      zmm_t zmm_values[HI16_ZMM_TEST_REG_COUNT] __attribute__((aligned(64)));
      __asm__ volatile("vmovdqa32 %%zmm16, (%0)\n"
                       "vmovdqa32 %%zmm17, 0x40(%0)\n"
                       "vmovdqa32 %%zmm18, 0x80(%0)\n"
                       "vmovdqa32 %%zmm19, 0xC0(%0)\n"
                       "vmovdqa32 %%zmm20, 0x100(%0)\n"
                       "vmovdqa32 %%zmm21, 0x140(%0)\n"
                       "vmovdqa32 %%zmm22, 0x180(%0)\n"
                       "vmovdqa32 %%zmm23, 0x1C0(%0)\n"
                       "vmovdqa64 %%zmm24, 0x200(%0)\n"
                       "vmovdqa64 %%zmm25, 0x240(%0)\n"
                       "vmovdqa64 %%zmm26, 0x280(%0)\n"
                       "vmovdqa64 %%zmm27, 0x2C0(%0)\n"
                       "vmovdqa64 %%zmm28, 0x300(%0)\n"
                       "vmovdqa64 %%zmm29, 0x340(%0)\n"
                       "vmovdqa64 %%zmm30, 0x380(%0)\n"
                       "vmovdqa64 %%zmm31, 0x3C0(%0)\n"
                       :
                       : "r"(zmm_values));
      for (int index = 0; index < HI16_ZMM_TEST_REG_COUNT; index++) {
         u_int64_t expected = get_value(cptr->index, index);
         if ((zmm_values[index].d7 != expected) || (zmm_values[index].d6 != expected) ||
            (zmm_values[index].d5 != expected) || (zmm_values[index].d4 != expected) ||
            (zmm_values[index].d3 != expected) || (zmm_values[index].d2 != expected) ||
            (zmm_values[index].d1 != expected) || (zmm_values[index].d0 != expected)) {
            cptr->failed_count++;
         }
      }
   }
}

void synchronize_threads(child_t* cptr, atomic_uint* ptr)
{
   atomic_uint value;

   pthread_mutex_lock(&test.cv_mutex);
   value = atomic_fetch_add(ptr, 1);
   if (value < (MAX_THREADS - 1)) {
      pthread_cond_wait(&test.cv, &test.cv_mutex);
   } else {
      pthread_cond_broadcast(&test.cv);
   }
   pthread_mutex_unlock(&test.cv_mutex);
}

void* child_thread(void* arg)
{
   child_t* cptr = (child_t*)arg;

   /* wait for all threads to start */
   synchronize_threads(cptr, &test.thread_started_count);

   set_xtended_registers(cptr);

   /* wait for all threads to complete register initialization */
   synchronize_threads(cptr, &test.thread_set_count);

   verify_xtended_registers(cptr);

   /* wait for all threads to complete verifcation */
   synchronize_threads(cptr, &test.thread_verify_count);

   return (void*)(u_int64_t)cptr->index;
}

TEST test_context_switch(void)
{
   pthread_mutex_init(&test.cv_mutex, NULL);
   pthread_cond_init(&test.cv, NULL);
   atomic_init(&test.thread_started_count, 0);
   atomic_init(&test.thread_set_count, 0);
   atomic_init(&test.thread_verify_count, 0);

   for (int i = 0; i < MAX_THREADS; i++) {
      int status;
      child_t* cptr = &test.child[i];

      cptr->index = i;
      cptr->failed_count = 0;

      status = pthread_create(&cptr->thread_id, NULL, child_thread, cptr);
      ASSERT_EQ(0, status);
   }

   for (int i = 0; i < MAX_THREADS; i++) {
      int status;
      child_t* cptr = &test.child[i];

      status = pthread_join(cptr->thread_id, &cptr->thread_exit_value);
      ASSERT_EQ(0, status);

      ASSERT_EQ(0, cptr->failed_count);
   }
   PASS();
}

void signal_handler(int signal)
{
   child_t child;

   child.index = SIGNAL_PATTERN;
   child.failed_count = 0;

   set_xtended_registers(&child);
}

TEST test_signal(void)
{
   child_t child;

   child.index = MAIN_PATTERN;
   child.failed_count = 0;

   set_xtended_registers(&child);

   signal(SIGTERM, signal_handler);

   kill(0, SIGTERM);

   verify_xtended_registers(&child);
   ASSERT_EQ(0, child.failed_count);

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();   // init & parse command-line args

   memset(&test, 0, sizeof(test_t));

   get_xtended_features();

   for (int i = 0; i < XSTATE_ITER_COUNT; i++) {
      RUN_TEST(test_context_switch);
   }

   RUN_TEST(test_signal);

   GREATEST_PRINT_REPORT();
   return greatest_info.failed;   // return count of errors (or 0 if all is good)
}
