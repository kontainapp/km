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
 * A small program to test popen() in 2 ways:
 * - write the contents of a file to pipetarget_test via a pipe amd cat writes the data to a file
 * - pipetarget_test reads a file and writes to stdout which is a pipe and this program reads the data and
 *   writes to a file.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

char* progname;

static void usage(void)
{
   fprintf(stderr, "Usage:\n  %s data_file outfile1 outfile2\n", progname);
}

int main(int argc, char* argv[])
{
   FILE* p;
   char linebuf[1024];
   char cmdbuf[512];
   int exit_status;
   char* testprog = "./pipetarget_test";

   progname = argv[0];

   if (argc != 4) {
      usage();
      return 1;
   }

   // Cleanup
   unlink(argv[2]);
   unlink(argv[3]);

   char* env_testprog = getenv("TESTPROG");
   if (env_testprog != NULL) {
      fprintf(stderr, "Override testprog %s with %s\n", testprog, env_testprog);
      testprog = env_testprog;
   }

   snprintf(cmdbuf, sizeof(cmdbuf), "%s writetoparent %s", testprog, argv[1]);
   FILE* o;
   o = fopen(argv[2], "w");
   if (o == NULL) {
      fprintf(stderr, "couldn't open %s for write, %s\n", argv[2], strerror(errno));
      exit(1);
   }
   p = popen(cmdbuf, "r");
   if (p == NULL) {
      fclose(o);
      fprintf(stderr, "popen %s failed, %s\n", cmdbuf, strerror(errno));
      return 1;
   }
   while (fgets(linebuf, sizeof(linebuf), p) != NULL) {
      fprintf(o, "%s", linebuf);
   }
   fclose(o);
   exit_status = pclose(p);
   fprintf(stdout, "command: %s, exit status 0x%x\n", cmdbuf, exit_status);
   if (exit_status != 0) {
      return 1;
   }

   snprintf(cmdbuf, sizeof(cmdbuf), "%s readfromparent %s", testprog, argv[3]);
   FILE* i;
   i = fopen(argv[1], "r");
   if (i == NULL) {
      fprintf(stderr, "couldn't open %s for read, %s\n", argv[1], strerror(errno));
      exit(1);
   }
   p = popen(cmdbuf, "w");
   if (p == NULL) {
      fclose(i);
      fprintf(stderr, "popen %s failed, %s\n", cmdbuf, strerror(errno));
      return 1;
   }
   while (fgets(linebuf, sizeof(linebuf), i) != NULL) {
      fprintf(p, "%s", linebuf);
   }
   fclose(i);
   exit_status = pclose(p);
   fprintf(stdout, "command: %s, exit status 0x%x\n", cmdbuf, exit_status);
   if (exit_status != 0) {
      return 1;
   }

   return 0;
}
