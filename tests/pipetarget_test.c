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
 * This is a small program used by the popen_test program as the command supplied to popen().
 * You can write data to it via the pipe and the data is written to a file.
 * Or this program can read data from a file and write it into the pipe back to the popen_test program.
 *
 *  pipetarget_test readfrompipe destfile
 *  pipetarget_test writetopipe sourcefile
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>

static char* PIPEWRITE = "writetoparent";
static char* PIPEREAD = "readfromparent";

void usage(void)
{
   fprintf(stderr, "pipetarget_test {%s|%s} filename\n",
           PIPEWRITE, PIPEREAD);
}

int main(int argc, char* argv[])
{
   char linebuf[1024];
   if (argc != 3) {
      usage();
      return 1;
   }
   if (strcmp(argv[1], PIPEREAD) == 0) {  // read from pipe, write into file
      FILE* o = fopen(argv[2], "w");
      if (o != NULL) {
         while (fgets(linebuf, sizeof(linebuf), stdin) != NULL) {
            fputs(linebuf, o);
         }
         fclose(o);
      } else {
         fprintf(stderr, "couldn't open %s for write, %s\n", argv[2], strerror(errno));
         return 1;
      }
   } else if (strcmp(argv[1], PIPEWRITE) == 0) {  // open file for read, write into pipe
      FILE* i = fopen(argv[2], "r");
      if (i != NULL) {
         while (fgets(linebuf, sizeof(linebuf), i) != NULL) {
            fputs(linebuf, stdout);
         }
         fclose(i);
      } else {
         fprintf(stderr, "couldn't open %s for read, %s\n", argv[2], strerror(errno));
         return 1;
      }
   } else {
      fprintf(stderr, "Unknown operation %s, must be either %s or %s\n",
              argv[1],
              PIPEWRITE,
              PIPEREAD);
      return 1;
   }
   return 0;
}
