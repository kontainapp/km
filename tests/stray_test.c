/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Generates exceptions in guest in order to test guest coredumps.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "km_hcalls.h"
#include "syscall.h"

char* cmdname = "?";

// make the data and bss segments large to force a region boundary crossing in coredump.
int bigarray_data[10000000] = {1, 2, 3};
int bigarray_bss[10000000];

// Generate a X86 #PF exception
void stray_reference(void)
{
   char* ptr = (char*)-16;
   *ptr = 'a';
}

// Generate a X86 #DE exception
void div0()
{
   asm volatile("xor %rcx, %rcx\n\t"
                "div %rcx");
}

// Generate a X86 #UD excpetion
void undefined_op()
{
   asm volatile("ud2");
}

void bad_hcall()
{
   km_hc_args_t arg;
   km_hcall(SYS_fork, &arg);
}

void write_text(void* ptr)
{
   *((char*)ptr) = 'a';
}

pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;
void* thread_main(void* arg)
{
   pthread_mutex_lock(&mt);
   return NULL;
}

void signal_handler(int sig)
{
   abort();
}

void usage()
{
   fprintf(stderr, "usage: %s <operation>\n", cmdname);
   fprintf(stderr, "operations:\n");
   fprintf(stderr, " stray - generate a stray reference\n");
   fprintf(stderr, " div0  - generate a divide by zero\n");
   fprintf(stderr, " ud    - generate an undefined op code\n");
   fprintf(stderr, " hc    - generate an undefined hypercall\n");
   fprintf(stderr, " prot  - generate an write to protected memory\n");
   fprintf(stderr, " abort - generate an abort call\n");
   fprintf(stderr, " quit  - generate a SIGQUIT\n");
   fprintf(stderr, " term  - generate a SIGTERM\n");
   fprintf(stderr, " signal- abort() inside signal handler\n");
}

int main(int argc, char** argv)
{
   extern int optind;
   int c;
   char* op;
   pthread_t thr;

   cmdname = argv[0];
   while ((c = getopt(argc, argv, "h")) != -1) {
      switch (c) {
         case 'h':
            usage();
            return 0;
         default:
            fprintf(stderr, "unrecognized option %c\n", c);
            usage();
            return 1;
      }
   }
   if (optind + 1 != argc) {
      usage();
      return 1;
   }
   op = argv[optind];

   pthread_mutex_lock(&mt);
   if (pthread_create(&thr, NULL, thread_main, NULL) != 0) {
      fprintf(stderr, "pthread_create failed - %d\n", errno);
      return 1;
   }

   /*
    * Map a fairly large region to force the coredump to do a large write.
    */
   if (mmap(0, 10 * 1024 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) ==
       MAP_FAILED) {
      fprintf(stderr, "large mmap failed");
      return 1;
   }

   if (strcmp(op, "stray") == 0) {
      stray_reference();
      return 1;
   }
   if (strcmp(op, "div0") == 0) {
      div0();
      return 1;
   }
   if (strcmp(op, "ud") == 0) {
      undefined_op();
      return 1;
   }
   if (strcmp(op, "hc") == 0) {
      bad_hcall();
      return 1;
   }
   if (strcmp(op, "prot") == 0) {
      write_text(usage);
      return 1;
   }
   if (strcmp(op, "abort") == 0) {
      abort();
      return 1;
   }
   if (strcmp(op, "quit") == 0) {
      kill(0, SIGQUIT);
      return 1;
   }
   if (strcmp(op, "term") == 0) {
      kill(0, SIGTERM);
      return 1;
   }
   if (strcmp(op, "signal") == 0) {
      signal(SIGUSR1, signal_handler);
      kill(0, SIGUSR1);
      return 1;
   }
   fprintf(stderr, "Unrecognized operation '%s'\n", op);
   usage();
   return 1;
}