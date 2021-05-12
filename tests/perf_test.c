/*
 * Copyright © 2020-2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Measure time for dummy hypercall
 * Measure time for pagefault
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/times.h>
#include <sys/types.h>
#include "greatest/greatest.h"

/*
 * getuid boils down to a dummy call currently
 * use it to measure rtt
 */
#define LOOP_COUNT (10 * 1024)   // loops for mmap tests
#define TEST_PAGE_SIZE (4096ULL)
#define NSEC_PER_SEC (1000 * 1000 * 1000ULL)
#define NSEC_PER_MILLI_SEC (1000 * 1000ULL)

typedef struct {
   struct {   // data collected with times()
      struct tms start;
      struct tms end;
      long ticks_per_sec;
      float calibrate;   // total for calibration  run
   } times;

   struct {   // data collected with clock_gettime(CLOCK_PROCESS_CPUTIME_ID)
      struct timespec start;
      struct timespec end;
      u_int64_t nsec_consumed;
      float calibrate;   // total for calibration  run
   } clock_process;
   struct {   // data collected with clock_gettime(CLOCK_THREAD_CPUTIME_ID)
      struct timespec start;
      struct timespec end;
      u_int64_t nsec_consumed;
      float calibrate;   // total for calibration  run
   } clock_thread;
} sample_t;

static sample_t g_sample;

static const long calibrate_loops = (long)1e7;
static const long hypercall_loops = (long)1e4;
static const long mmap_loops = (long)1e4;   // CAUTION: 1e4 is max, otherwise we are OOM

/*
 *  allow tests to run calibrate-ratio x calibration
 * It heavily depends on the virtualization nesting and kkm/kvm
 * on L0, it is expected to be ~6
 * on L1 KKM , ~30-40
 * with KVM load, it seems to go way up. So this test may fail intermittently
 * with high KVM load (multiple VMs come in go fast)
 */
static const int hcall_calibrate_ratio = 500;
static const int mmap_calibrate_ratio = 10;

static long busy_loop(long i)
{
   volatile long x;
   for (; i > 0; i--) {
      x += i * i;
   }
   return x;
}

// validate and prints time spent between end and start
/// 'ratio' is tricky:
// if ratio == -1, it prints and  calibrates
// if ratio == 0, it only prints
// if ratio > 0, it prints and asserts the values are under ratio*calibration
TEST validate_time(char* name, sample_t* s, int ratio)
{
   float user_sec, system_sec;
   float total_times, total_clock_process, total_clock_thread;
   u_int64_t start_nsec, end_nsec;

   // times() accounting
   user_sec = ((float)s->times.end.tms_utime - s->times.start.tms_utime) / s->times.ticks_per_sec;
   system_sec = ((float)s->times.end.tms_stime - s->times.start.tms_stime) / s->times.ticks_per_sec;
   total_times = user_sec + system_sec;
   printf("%s times: delta time: %.6f (user: %.6f, system: %.6f)\n", name, total_times, user_sec, system_sec);

   // process
   start_nsec = s->clock_process.start.tv_sec * NSEC_PER_SEC + s->clock_process.start.tv_nsec;
   end_nsec = s->clock_process.end.tv_sec * NSEC_PER_SEC + s->clock_process.end.tv_nsec;
   s->clock_process.nsec_consumed = end_nsec - start_nsec;
   total_clock_process = (float)s->clock_process.nsec_consumed / NSEC_PER_SEC;
   printf("%s clock_process: delta time: %.6f\n", name, total_clock_process);

   // thread
   start_nsec = s->clock_thread.start.tv_sec * NSEC_PER_SEC + s->clock_thread.start.tv_nsec;
   end_nsec = s->clock_thread.end.tv_sec * NSEC_PER_SEC + s->clock_thread.end.tv_nsec;
   s->clock_thread.nsec_consumed = end_nsec - start_nsec;
   total_clock_thread = (float)s->clock_thread.nsec_consumed / NSEC_PER_SEC;
   printf("%s clock_thead: delta time: %.6f\n", name, total_clock_thread);

   if (ratio == -1) {
      s->times.calibrate = total_times;
      s->clock_process.calibrate = total_clock_process;
      s->clock_thread.calibrate = total_clock_thread;
   }

   if (ratio > 0) {
      printf("Validating %s\n", name);
      ASSERTm("Times", total_times < s->times.calibrate * ratio);
      ASSERTm("clock_process", total_clock_process < s->clock_process.calibrate * ratio);
      ASSERTm("clock_thread", total_clock_thread < s->clock_thread.calibrate * ratio);
   }

   PASS();
}

// records times
// start == 1 captures start time, start == 0 captures end times
TEST capture_time(sample_t* s, int start)
{
   struct timespec* time_process;
   struct timespec* time_thread;
   struct tms* time_times;

   if (start == 1) {
      time_process = &s->clock_process.start;
      time_thread = &s->clock_thread.start;
      time_times = &s->times.start;
   } else {
      time_process = &s->clock_process.end;
      time_thread = &s->clock_thread.end;
      time_times = &s->times.end;
   }
   if (times(time_times) == -1 || clock_gettime(CLOCK_PROCESS_CPUTIME_ID, time_process) == -1 ||
       clock_gettime(CLOCK_THREAD_CPUTIME_ID, time_thread) == -1) {
      perror("capture_time");
      FAIL();
   }
   PASS();
}

TEST calibrate_relative_speed(sample_t* s)
{
   CHECK_CALL(capture_time(s, 1));
   busy_loop(calibrate_loops);
   CHECK_CALL(capture_time(s, 0));

   CHECK_CALL(validate_time("calibrate", s, -1));
   PASS();
}

TEST hypercall_time(sample_t* s)
{
   volatile uid_t uid = 0;

   CHECK_CALL(capture_time(s, 1));
   for (int i = 0; i < hypercall_loops; i++) {
      uid = getuid();
      (void)uid;
   }
   sleep(1);   // let's add one sec to make sure we are not measuring wall clock time
   CHECK_CALL(capture_time(s, 0));
   CHECK_CALL(validate_time("hcall (times)", s, hcall_calibrate_ratio));
   PASS();
}

TEST mmap_time(sample_t* s, bool write)
{
   int ret_val = 0;
   volatile char* addr = NULL;
   volatile char value = 0;
   size_t map_size = TEST_PAGE_SIZE * LOOP_COUNT;

   addr = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   ASSERT_NEQm("mmap failed", addr, MAP_FAILED);

   CHECK_CALL(capture_time(s, 1));
   for (int i = 0; i < mmap_loops; i++) {
      if (write == true) {
         *(addr + TEST_PAGE_SIZE * i) = value;
      } else {
         value = *(addr + TEST_PAGE_SIZE * i);
      }
   }
   ret_val = munmap((void*)addr, map_size);
   ASSERT_EQm("munmap failed", ret_val, 0);
   CHECK_CALL(capture_time(s, 0));

   char* name = (write == false) ? "mmap read" : "mmap write";
   CHECK_CALL(validate_time(name, s, mmap_calibrate_ratio));

   PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   g_sample.times.ticks_per_sec = sysconf(_SC_CLK_TCK);
   RUN_TESTp(calibrate_relative_speed, &g_sample);
   RUN_TESTp(hypercall_time, &g_sample);
   RUN_TESTp(mmap_time, &g_sample, false);
   RUN_TESTp(mmap_time, &g_sample, true);

   GREATEST_MAIN_END();
}
