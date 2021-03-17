/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
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
