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
 * Generates exceptions in guest.
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

pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;
void* thread_main(void* arg)
{
   pthread_mutex_lock(&mt);
   return NULL;
}

void usage()
{
   fprintf(stderr, "usage: %s <operation>\n", cmdname);
   fprintf(stderr, "operations:\n");
   fprintf(stderr, " stray - generate a stray reference\n");
   fprintf(stderr, " div0  - generate a divde by zero\n");
   fprintf(stderr, " ud    - generate an undefined op code\n");
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
      km_hc_args_t arg;
      km_hcall(SYS_fork, &arg);
      return 1;
   }
   fprintf(stderr, "Unrecognized operation '%s'\n", op);
   usage();
   return 1;
}