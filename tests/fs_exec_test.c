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
   int fd = open("/proc/self/cmdline", O_RDONLY);

   if (argc > 1 && strcmp(argv[1], "parent") == 0) {
      char parent_buf[256];
      int parent_rc = read(fd, parent_buf, sizeof(parent_buf));
      lseek(fd, SEEK_SET, 0);
      printf("parent cmdline:");
      for (int i = 0; i < parent_rc;) {
         i += printf(" %s", parent_buf + i);
      }
      printf("\n");
      char* testargv[] = {"fs_exec_test", "child", NULL};
      int rc = execve("fs_exec_test", testargv, environ);
      printf("exec %d %s\n", rc, strerror(errno));
   } else {
      char buf[256];
      int child_rc = read(fd, buf, sizeof(buf));
      printf("child  cmdline:");
      for (int i = 0; i < child_rc;) {
         i += printf(" %s", buf + i);
      }
      printf("\n");
   }
}