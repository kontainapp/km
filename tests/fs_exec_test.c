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

extern char** environ;

int main(int argc, char** argv)
{
   char* name = "/proc/self/exe";

   if (argc > 1 && strcmp(argv[1], "parent") == 0) {
      char parent_buf[256];
      int parent_rc = readlink(name, parent_buf, sizeof(parent_buf));
      if (parent_rc > 0) {
         printf("parent exe: %.*s %s\n", parent_rc, parent_buf, argv[1]);
      }
      char* testargv[] = {"fs_exec_test", "child", NULL};
      int rc = execve("fs_exec_test", testargv, environ);
      printf("exec %d %s\n", rc, strerror(errno));
   } else {
      char buf[256];
      int child_rc = readlink(name, buf, sizeof(buf));
      if (child_rc > 0) {
         printf("child  exe: %.*s %s\n", child_rc, buf, argv[1]);
      }
   }
}