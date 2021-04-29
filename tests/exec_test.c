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
 * -s to tests /bin/sh parse
 * -S to tests /bin/env (and others) parse via shebang exec
 * -X to test exec into realpath of /proc/self/exe
 *
 * Set KM_EXEC_TEST_EXE environment to override what it being exec-ed into by default
 *
 * TODO: execveat() is only tested for fexecve() subset, need to add test
 *
 */

#define EXEC_TEST_EXE_ENV "KM_EXEC_TEST_EXE"
#define EXEC_TEST_EXE_DEFAULT "print_argenv_test"
#define EXEC_TEST_EXE_KM "hello_test.km"

#define EXEC_TEST_EXE_ENOENT "this_file_should_not_exist.ever"
#define EXEC_TEST_EXE_ENOEXEC "test_helper.bash"   // existing file but not an ELF

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
              "Expected failure: execve()  rc %d, errno %d, %s: %s\n",
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
   } else if (strcmp(argv[1], "-s") == 0) {   // exec into /bin/sh
      if (KM_PAYLOAD() == 0) {
         fprintf(stderr, "not in payload - nothing to do here\n");
         return 0;
      }
      // sanity check for /bin/sh parse. Note that it works ONLY for .km pass
      char* testargv[] = {"/bin/sh",
                          "-c",
                          "./hello_test.km --parse \"string with quotes\" 'more\\ quotes' ! ;",
                          NULL};
      rc = execve("/bin/sh", testargv, testenvp);
      fprintf(stderr, "execve() failed, rc %d, errno %d, %s\n", rc, errno, strerror(errno));
   } else if (strcmp(argv[1], "-S") == 0) {   // exec into shebang /bin/env
      char* testargv[] = {"/bin/env", "./hello_test.km", NULL};
      if (KM_PAYLOAD() == 0) {
         testargv[1] = "./hello_test.fedora";
      }
      rc = execve("/bin/env", testargv, testenvp);
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
