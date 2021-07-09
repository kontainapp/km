/*
 * Copyright 2021 Kontain Inc.
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
         fflush(stdout);
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