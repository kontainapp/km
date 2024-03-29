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
 * A simple test to create a shared memory segment, create a semaphore in the shared memory
 * segment, then fork() a child process, and then have parent and child operate on the
 * shared memory segment using a semaphore to serialize access to the shared memory.
 */

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "greatest/greatest.h"
#include "mmap_test.h"
GREATEST_MAIN_DEFS();

const static int shared_mem_size = 3 * 4096;

#define PARENT_SIDE "parent side"
#define CHILD_SIDE "child side"

#define STRING_SIZE 128
typedef struct sm {
   sem_t semaphore;
   char string[STRING_SIZE];
   char parent_side[STRING_SIZE];
   char child_side[STRING_SIZE];
} sm_t;

int fd = -1;   // file to mmap, -1 for ANON
char fname[] = "/tmp/sem_testXXXXXX";
int initial_busy_count;

/*
 * To verify that mmap() operations did what we expected, it is useful to see what
 * /proc/self/maps says after an operation completes.  This function dumps a copy
 * onto stdout.
 */
#define MAX_EXPECTED_MAPS_FILE_SIZE (16 * 1024)
void catprocpidmaps(char* tag)
{
   char* procpidmaps = "/proc/self/maps";

   if (GREATEST_IS_VERBOSE() == 0) {
      return;
   }

   fprintf(stdout, "Reading maps %s:\n", tag);
   int fd = open(procpidmaps, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Couldn't open %s, %s\n", procpidmaps, strerror(errno));
      return;
   }
   char buf[MAX_EXPECTED_MAPS_FILE_SIZE];
   ssize_t br = read(fd, buf, sizeof(buf) - 1);
   if (br < 0) {
      fprintf(stderr, "Couldn't read %s, %s\n", procpidmaps, strerror(errno));
   } else {
      buf[br] = 0;
      fputs(buf, stdout);
      fflush(stdout);
   }
   close(fd);
}

TEST work_func(sm_t* smp, char* tag)
{
   char string[128];
   int rv;

   if (GREATEST_IS_VERBOSE()) {
      fprintf(stdout, "begin: %s, smp %p\n", tag, smp);
   }
   for (int j = 0; j < 5; j++) {
      rv = sem_wait(&smp->semaphore);
      ASSERT_NEQm("sem_wait() failed", -1, rv);

      if (GREATEST_IS_VERBOSE()) {
         fprintf(stdout, "%s: holding semaphore\n", tag);
      }

      for (int i = 0; i < 20; i++) {
         snprintf(string, sizeof(string), "%s %d", tag, i);
         strcpy(smp->string, string);
         struct timespec ts = {0, 1000000};
         if (nanosleep(&ts, NULL) < 0 && errno != EINTR) {
            fprintf(stderr, "%s: pid %d: nanosleep() failed, %s\n", __FUNCTION__, getpid(), strerror(errno));
            FAIL();
         }
         ASSERT_STR_EQm("test string miscompare", string, smp->string);
      }
      rv = sem_post(&smp->semaphore);
      ASSERT_NEQm("sem_post() failed", -1, rv);

      if (GREATEST_IS_VERBOSE()) {
         fprintf(stdout, "%s: released semaphore\n", tag);
      }

      struct timespec delay = {0, 2000000};
      clock_nanosleep(CLOCK_REALTIME, 0, &delay, NULL);   // give the other side a chance to get the
                                                          // semaphore
   }
   if (GREATEST_IS_VERBOSE()) {
      fprintf(stdout, "end: %s\n", tag);
   }
   PASS();
}

TEST child_side(sm_t* smp)
{
   strcpy(smp->child_side, CHILD_SIDE);
   catprocpidmaps(CHILD_SIDE);

   int rv = work_func(smp, CHILD_SIDE);
   GREATEST_CHECK_CALL(rv);

   if (GREATEST_IS_VERBOSE()) {
      fprintf(stdout,
              "%s: child_side[] \"%s\", parent_side[] \"%s\"\n",
              __FUNCTION__,
              smp->child_side,
              smp->parent_side);
   }

   ASSERT_STR_EQm("child doesn't see parent's changes in shared memory", PARENT_SIDE, smp->parent_side);

   void* tmp;

   /*
    * We are in child after fork. In order for ASSERT_MMAPS_* family to work gdb has to run with
    * "set follow-fork-mode child"
    */
   ASSERT_MMAPS_CHANGE(1, initial_busy_count);   // initial shared region

   // Make the middle 4k of the shared memory private.  This should add 2 busy map entries.
   tmp =
       mmap((void*)smp + 4096, 4096, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
   ASSERT_NEQm("couldn't unshare middle of memory region", MAP_FAILED, tmp);
   catprocpidmaps(CHILD_SIDE " after marking middle of mem private");

   ASSERT_MMAPS_CHANGE(3, initial_busy_count);   // shared, private, shared

   // Change the child's copy of shared memory to private. This should collapse 3 busy entries to one.
   tmp = mmap(smp, shared_mem_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
   ASSERT_NEQm("couldn't unshare memory region", MAP_FAILED, tmp);

   catprocpidmaps(CHILD_SIDE " after marking mem private");

   ASSERT_MMAPS_CHANGE(0, initial_busy_count);   // now all private, so it merges with stack

   PASS();
}

TEST parent_side(sm_t* smp)
{
   strcpy(smp->parent_side, PARENT_SIDE);
   catprocpidmaps(PARENT_SIDE);

   int rv = work_func(smp, PARENT_SIDE);
   GREATEST_CHECK_CALL(rv);

   if (GREATEST_IS_VERBOSE()) {
      fprintf(stdout,
              "%s: child_side[] \"%s\", parent_side[] \"%s\"\n",
              __FUNCTION__,
              smp->child_side,
              smp->parent_side);
   }

   ASSERT_STR_EQm("parent doesn't see child's changes to shared memory", CHILD_SIDE, smp->child_side);

   PASS();
}

TEST shared_semaphore(void)
{
   sm_t* smp;
   int rv = 0;

   ASSERT_MMAPS_INIT(initial_busy_count);

   // create shared memory segment
   int flags = MAP_SHARED | (fd == -1 ? MAP_ANON : 0);
   smp = mmap(NULL, shared_mem_size, PROT_READ | PROT_WRITE, flags, fd, 0);
   ASSERT_NEQm("couldn't create shared mem segment", MAP_FAILED, smp);

   // init semaphore in the shared memory
   rv = sem_init(&smp->semaphore, 1, 1);
   ASSERT_NEQm("couldn't init semaphore", -1, rv);

   // fork a child process
   pid_t pid = fork();
   ASSERT_NEQm("couldn't fork", -1, pid);
   if (GREATEST_IS_VERBOSE()) {
      fprintf(stdout, "fork() returns %d, getpid() = %d\n", pid, getpid());
   }
   if (pid == 0) {   // child process
      CHECK_CALL(child_side(smp));
      PASS();
      // back to main() to report results from child
   } else {
      CHECK_CALL(parent_side(smp));

      int wstatus;
      pid_t reaped_pid;
      reaped_pid = waitpid(pid, &wstatus, 0);
      ASSERT_EQm("waitpid didn't return expected pid", pid, reaped_pid);
      ASSERT_NEQm("child did not exit normally", 0, WIFEXITED(wstatus));
      ASSERT_EQm("child returned non-zero exit status", 0, WEXITSTATUS(wstatus));

      if (GREATEST_IS_VERBOSE()) {
         fprintf(stdout, "child pid %d, wstatus 0x%x\n", pid, wstatus);
      }
   }
   rv = sem_destroy(&smp->semaphore);
   ASSERT_NEQm("sem_destroy() failed", -1, rv);

   rv = munmap(smp, shared_mem_size);
   ASSERT_NEQm("munmap() failed", -1, rv);

   catprocpidmaps(PARENT_SIDE " after unmapping shared mem");

   PASS();
}

void cleanup(void)
{
   if (fd >= 0) {
      close(fd);
      unlink(fname);
   }
}

int main(int argc, char* argv[])
{
   GREATEST_MAIN_BEGIN();

   // if requested create and open file to back up memory
   if (argc > 1 && strcmp(argv[1], "file") == 0) {
      fd = mkstemp(fname);
      ASSERT_NEQm("mkstemp", -1, fd);
      int rc = ftruncate(fd, shared_mem_size);
      ASSERT_EQm("ftruncate(shared_mem_size)", 0, rc);
      atexit(cleanup);
   }

   if (GREATEST_IS_VERBOSE()) {
      fprintf(stdout, "shared memory region size %d bytes\n", shared_mem_size);
   }

   RUN_TEST(shared_semaphore);

   GREATEST_MAIN_END();
}
