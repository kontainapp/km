/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

/*
 * A simple test to create a shared memory segment, create a semphore in the shared memory
 * segment, then fork() a child process, and then have parent and child operate on the
 * shared memory segment using a semphore to serialize access to the shared memory.
 */

#include <stdio.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>

const static int shared_mem_size = 4096;

#define PARENT_SIDE "parent side"
#define CHILD_SIDE "child side"

typedef struct sm {
   sem_t semaphore;
   char string[128];
   char parent_side[128];
   char child_side[128];
} sm_t;

/*
 * To verify that mmap() operations did what we expected, it is useful to see what
 * /proc/self/maps says after an operation completes.  This function dumps a copy
 * onto stdout.
 */
void catprocpidmaps(char* tag)
{
   char* procpidmaps = "/proc/self/maps";

   fprintf(stdout, "Reading maps %s:\n", tag);
   int fd = open(procpidmaps, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Couldn't open %s, %s\n", procpidmaps, strerror(errno));
      return;
   }
   char buf[16*1024];
   size_t br = read(fd, buf, sizeof(buf));
   if (br < 0) {
      fprintf(stderr, "Couldn't read %s, %s\n", procpidmaps, strerror(errno));
   } else {
      buf[br] = 0;
      fputs(buf, stdout);
      fflush(stdout);
   }
   close(fd);
}


int work_func(sm_t* smp, char* tag)
{
   char string[128];

   fprintf(stdout, "begin: %s, smp %p\n", tag, smp);
   for (int j = 0; j < 5; j++) {
      if (sem_wait(&smp->semaphore) < 0) {
         fprintf(stderr, "%s: pid %d: sem_wait() failed, %s\n", __FUNCTION__, getpid(), strerror(errno));
         return 1;
      }
      fprintf(stdout, "%s: holding semaphore\n", tag);
      
      for (int i = 0; i < 20; i++) {
         snprintf(string, sizeof(string), "%s %d", tag, i);
         strcpy(smp->string, string);
         struct timespec ts = { 0, 1000000 };
         if (nanosleep(&ts, NULL) < 0 && errno != EINTR) {
            fprintf(stderr, "%s: pid %d: nanosleep() failed, %s\n", __FUNCTION__, getpid(), strerror(errno));
            return 1;
         }
         if (strcmp(smp->string, string) != 0) {
            fprintf(stderr, "%s: pid %d: string miscompare, expected %s, found %s\n", __FUNCTION__, getpid(), string, smp->string);
         }
      }
      if (sem_post(&smp->semaphore) < 0) {
         fprintf(stderr, "%s: pid %d: sem_post() failed, %s\n", __FUNCTION__, getpid(), strerror(errno));
         return 1;
      }
      fprintf(stdout, "%s: released semaphore\n", tag);
      struct timespec delay = {0,2000000};
      nanosleep(&delay, NULL);    // give the other side a chance to get the semaphore
   }
   fprintf(stdout, "end: %s\n", tag);
   return 0;
}

int child_side(sm_t* smp)
{
   strcpy(smp->child_side, CHILD_SIDE);
   catprocpidmaps(CHILD_SIDE);
   int rv = work_func(smp, CHILD_SIDE);
   fprintf(stdout, "%s: child_side[] \"%s\", parent_side[] \"%s\"\n", __FUNCTION__, smp->child_side, smp->parent_side);
   if (strcmp(smp->parent_side, PARENT_SIDE) != 0) {
      fprintf(stderr, "%s does not see changes made to shared mem by parent\n", CHILD_SIDE);
      rv = 1;
   }
   // Change the childs copy of shared memory to private.
   void* tmp = mmap(smp, shared_mem_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
   if (tmp == MAP_FAILED) {
      fprintf(stderr, "%s: %s couldn't set shared mem segment to private, %s\n", __FUNCTION__, CHILD_SIDE, strerror(errno));
      rv = 1;
   } else {
      catprocpidmaps(CHILD_SIDE " after marking mem private");
   }
   return rv;
}

int parent_side(sm_t* smp)
{
   strcpy(smp->parent_side, PARENT_SIDE);
   catprocpidmaps(PARENT_SIDE);
   int rv = work_func(smp, PARENT_SIDE);
   fprintf(stdout, "%s: child_side[] \"%s\", parent_side[] \"%s\"\n", __FUNCTION__, smp->child_side, smp->parent_side);
   if (strcmp(smp->child_side, CHILD_SIDE) != 0) {
      fprintf(stderr, "%s does not see changes made to shared mem by child\n", PARENT_SIDE);
      rv = 1;
   }
   return rv;
}


int main(int argc, char* argv[])
{
   sm_t* smp;

   // create shared memory segment
   smp = mmap(NULL, shared_mem_size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
   if (smp == MAP_FAILED) {
      fprintf(stderr, "%s: couldn't create anon/shared mem segment, %s\n", __FUNCTION__, strerror(errno));
      return 1;
   }

   // init semaphore in the shared memory
   if (sem_init(&smp->semaphore, 1, 1) < 0) {
      fprintf(stderr, "%s: couldn't init semaphore, %s\n", __FUNCTION__, strerror(errno));
      return 1;
   }

   int rv = 0;

   // fork a child process
   pid_t pid = fork();
   switch (pid) {
      case -1:   // error
         break;
      case 0:    // child process
         exit(child_side(smp));
         break;
      default:   // parent process
         if (parent_side(smp) != 0) {
            rv++;
         }
         int wstatus;
         if (waitpid(pid, &wstatus, 0) != pid) {
            fprintf(stderr, "waitpid for pid %d failed, %s\n", pid, strerror(errno));
            rv++;
         } else {
            fprintf(stdout, "child pid %d, wstatus 0x%x\n", pid, wstatus);
            if (WIFEXITED(wstatus)) {
               if (WEXITSTATUS(wstatus) != 0) {
                  rv++;
               }
            } else {
               rv++;
            }
         }
         break;
   }
   if (sem_destroy(&smp->semaphore) < 0) {
      fprintf(stderr, "%s: sem_destroy() failed, %s\n", __FUNCTION__, strerror(errno));
      rv++;
   }
   if (munmap(smp, shared_mem_size) < 0) {
      fprintf(stderr, "%s: munmap() failed, %s\n", __FUNCTION__, strerror(errno));
      rv++;
   } else {
      catprocpidmaps(PARENT_SIDE " after unmapping shared mem");
   }
   return rv;
}
