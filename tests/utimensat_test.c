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

/*
 * Basic test for utimensat() call
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main()
{
   int fd;
   int ret;
   struct stat stbuf;
   char fname[256] = "/tmp/utimens_test_XXXXXX";
   struct timespec time_to_set[2] = {{1000000000, 111}, {1000000000, 222}};

   if ((fd = mkstemp(fname)) == -1) {
      fprintf(stderr, "mktemp fd %d %d %s\n", fd, errno, strerror(errno));
      return -1;
   }

   fstat(fd, &stbuf);
   fprintf(stderr,
           "%s before: atime %ld ns mtime %ld ns\n",
           fname,
           stbuf.st_atim.tv_nsec,
           stbuf.st_mtim.tv_nsec);

   usleep(100);
   if ((ret = utimensat(0, fname, time_to_set, 0)) == -1) {
      fprintf(stderr, "utimensat return %d %d %s\n", ret, errno, strerror(errno));
      return -1;
   }

   fstat(fd, &stbuf);
   close(fd);
   unlink(fname);

   fprintf(stderr,
           "%s after: atime %ld ns mtime %ld ns\n",
           fname,
           stbuf.st_atim.tv_nsec,
           stbuf.st_mtim.tv_nsec);

   // return 1 if all is good
   return (stbuf.st_atim.tv_nsec == time_to_set[0].tv_nsec &&
           stbuf.st_mtim.tv_nsec == time_to_set[1].tv_nsec)
              ? 0
              : -1;
}
