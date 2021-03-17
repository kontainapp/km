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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * A simple program to test execve() and execveat() (fexecve()) by exec to "print_argenv_test"
 *
 * Use the -f flag to test fexecve().
 * Use the -e flag to test ENOENT errno
 * Set KM_EXEC_TEST_EXE environment to override what it being exec-ed into
 *
 * TODO: execveat() is only tested for fexecve() subset, need to add test
 *
 */

#define EXEC_TEST_EXE_ENV "KM_EXEC_TEST_EXE"
#define EXEC_TEST_EXE_DEFAULT "print_argenv_test"
#define EXEC_TEST_EXE_ENOENT "this_file_should_not_exist.ever"

int main(int argc, char** argv)
{
   int rc;
   char* exec_test = getenv(EXEC_TEST_EXE_ENV);
   if (exec_test == NULL) {
      exec_test = EXEC_TEST_EXE_DEFAULT;
   }

   char* testargv[] = {exec_test, "a1", "b2", "c3", "d4", NULL};
   char* testenvp[] = {"ONE=one", "TWO=two", "THREE=three", "FOUR=four", NULL};

   if (argc == 2 && strcmp(argv[1], "-f") == 0) {
      int exefd = open(exec_test, O_RDONLY);
      if (exefd < 0) {
         fprintf(stderr, "open() of %s failed, %s\n", exec_test, strerror(errno));
      } else {
         fprintf(stderr, "Checking fexecve...\n");
         rc = fexecve(exefd, testargv, testenvp);
         fprintf(stderr, "fexecve() failed, errno %d, %s\n", errno, strerror(errno));
         close(exefd);
      }
   } else if (argc == 2 && strcmp(argv[1], "-e") == 0) {
      rc = execve(EXEC_TEST_EXE_ENOENT, testargv, testenvp);
      fprintf(stderr, "execve() rc %d, errno %d, %s: %s\n", rc, errno, strerror(errno), EXEC_TEST_EXE_ENOENT);
      return rc;
   } else {
      rc = execve(exec_test, testargv, testenvp);
      fprintf(stderr, "execve() failed, rc %d, errno %d, %s\n", rc, errno, strerror(errno));
   }

   return 99;
}
