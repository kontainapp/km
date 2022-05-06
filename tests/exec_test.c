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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "mmap_test.h"   // for KM_PAYLOAD definition

/*
 * A simple program to test execve() and execveat() (fexecve()) by exec to "print_argenv_test"
 *
 * Flags:
 * -f to test fexecve().
 * -e to test ENOENT errno
 * -E flag to test ENOEXEC errno
 * -k flags to test exec into '.km' file
 * -X to test exec into realpath of /proc/self/exe
 *
 * Set KM_EXEC_TEST_EXE environment to override what it being exec-ed into by default
 *
 * TODO: execveat() is only tested for fexecve() subset, need to add test
 *
 */

#define EXEC_TEST_EXE_ENV "KM_EXEC_TEST_EXE"
#define EXEC_TEST_EXE_DEFAULT "print_argenv_test.km"
#define EXEC_TEST_EXE_KM "hello_test.km"

#define EXEC_TEST_EXE_ENOENT "this_file_should_not_exist.ever"
#define EXEC_TEST_EXE_ENOEXEC "enoexec.helper"   // existing file but not an ELF

int main(int argc, char** argv)
{
   int rc;
   char* exec_test = getenv(EXEC_TEST_EXE_ENV);
   if (exec_test == NULL) {
      exec_test = EXEC_TEST_EXE_DEFAULT;
   }

   char* testargv[] = {exec_test, "a1", "b2", "c3", "d4", NULL};
   char* testenvp[] = {"ONE=one", "TWO=two", "THREE=three", "FOUR=four", NULL};

   if (argc == 1) {
      rc = execve(exec_test, testargv, testenvp);
      fprintf(stderr, "execve() failed, rc %d, errno %d, %s\n", rc, errno, strerror(errno));
      return 99;
   }

   if (strcmp(argv[1], "-f") == 0) {
      int exefd = open(exec_test, O_RDONLY);
      if (exefd < 0) {
         fprintf(stderr, "open() of %s failed, %s\n", exec_test, strerror(errno));
      } else {
         fprintf(stderr, "Checking fexecve...\n");
         rc = fexecve(exefd, testargv, testenvp);
         fprintf(stderr, "fexecve() failed, errno %d, %s\n", errno, strerror(errno));
         close(exefd);
      }
   } else if (strcmp(argv[1], "-e") == 0) {
      rc = execve(EXEC_TEST_EXE_ENOENT, testargv, testenvp);
      fprintf(stderr,
              "Expected failure: execve() rc %d, errno %d, %s: %s\n",
              rc,
              errno,
              strerror(errno),
              EXEC_TEST_EXE_ENOENT);
      return rc;
   } else if (strcmp(argv[1], "-E") == 0) {
      rc = execve(EXEC_TEST_EXE_ENOEXEC, testargv, testenvp);
      fprintf(stderr,
              "Expected failure: execve() rc %d, errno %d, %s: %s\n",
              rc,
              errno,
              strerror(errno),
              EXEC_TEST_EXE_ENOEXEC);
      return rc;
   } else if (strcmp(argv[1], "-k") == 0) {   // exec into '.km' file
      if (KM_PAYLOAD() == 0) {
         fprintf(stderr, "not in payload - nothing to do here\n");
         return 0;
      }
      char* testargv[] = {EXEC_TEST_EXE_KM, "TESTING exec to .km", "passing newline\n!", NULL};
      rc = execve(EXEC_TEST_EXE_KM, testargv, testenvp);
      fprintf(stderr, "execve() failed, rc %d, errno %d, %s\n", rc, errno, strerror(errno));
   } else if (strcmp(argv[1], "-X") == 0) {   // exec into /proc/self/exe
      char* testargv[] = {"/proc/self/exe", "-0", NULL};
      char buf[PATH_MAX];
      char* path = realpath(testargv[0], buf);
      assert(path != NULL);
      fprintf(stderr, "%s resolved to %s\n", testargv[0], path);

      rc = execve(path, testargv, testenvp);
      fprintf(stderr, "execve() failed, rc %d, errno %d, %s\n", rc, errno, strerror(errno));
   } else if (strcmp(argv[1], "-0") == 0) {   // noop, usually from exec-d program
      fprintf(stderr, "noop: -0 requested\n");
      return 0;
   } else {
      fprintf(stderr, "wrong flag %s\n", argv[1]);
      return 88;
   }

   return 99;
}
