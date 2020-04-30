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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
 * A simple program to test execve() and execveat() (fexecve())
 * Use the -f flag to test fexecve().
 */

#if 0
// Use this value if you want to run the test under linux
#define EXEC_TEST "print_argenv_test"
#else
// Use this value if you want to run this program as a km payload
#define EXEC_TEST "print_argenv_test.km"
#endif

int main(int argc, char** argv)
{
   int rc;
   char* testargv[] = { EXEC_TEST, "a1", "b2", "c3", "d4", NULL };
   char* testenvp[] = { "ONE=one", "TWO=two", "THREE=three", "FOUR=four", NULL };

   if (argc == 2 && strcmp(argv[1], "-f") == 0) {
      int exefd = open(EXEC_TEST, O_RDONLY);
      if (exefd < 0) {
         printf("open() of %s failed, %s\n", EXEC_TEST, strerror(errno));
      } else {
         rc = fexecve(exefd, testargv, testenvp);
         printf("fexecve() failed, errno %d, %s\n", errno, strerror(errno));
         close(exefd);
      }
   } else {
      rc = execve(EXEC_TEST, testargv, testenvp);
      printf("execve() failed, rc %d, errno %d, %s\n", rc, errno, strerror(errno));
   }

   return 99;
}
