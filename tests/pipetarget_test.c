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
