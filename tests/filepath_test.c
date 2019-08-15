/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Tests for file path oriented system calls
 */

#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "greatest/greatest.h"
#include "syscall.h"

char* dirpath = NULL;

/*
 * Open and close of eventfd.
 */
TEST test_mkdir()
{
   int rc;
   char newpath[PATH_MAX];
   snprintf(newpath, PATH_MAX, "%s/mydir", dirpath);
   rc = mkdir(newpath, 0777);
   ASSERT_EQ(0, rc);
   struct stat st;
   rc = stat(newpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   rc = rmdir(newpath);
   ASSERT_EQ(0, rc);

   rc = mkdir((char*)-1, 0777);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   rc = rmdir((char*)-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   PASS();
}

TEST test_rename()
{
   int rc;
   char oldpath[PATH_MAX];
   snprintf(oldpath, PATH_MAX, "%s/mydir", dirpath);
   rc = mkdir(oldpath, 0777);
   ASSERT_EQ(0, rc);
   struct stat st;
   rc = stat(oldpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   char newpath[PATH_MAX];
   snprintf(newpath, PATH_MAX, "%s/mydir2", dirpath);
   rc = rename(oldpath, newpath);
   ASSERT_EQ(0, rc);
   rc = stat(oldpath, &st);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(ENOENT, errno);
   rc = stat(newpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   rc = rmdir(newpath);
   ASSERT_EQ(0, rc);

   rc = rename((char*)-1, newpath);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   rc = rename(oldpath, (char*)-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   PASS();
}

TEST test_symlink()
{
   int rc;
   char oldpath[PATH_MAX];
   snprintf(oldpath, PATH_MAX, "%s/mydir", dirpath);
   rc = mkdir(oldpath, 0777);
   ASSERT_EQ(0, rc);

   char linkpath[PATH_MAX];
   snprintf(linkpath, PATH_MAX, "%s/mydir-link", dirpath);
   rc = symlink(oldpath, linkpath);
   ASSERT_EQ(0, rc);
   struct stat st;
   rc = lstat(linkpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFLNK, (st.st_mode & S_IFMT));

   rc = symlink((char*)-1, linkpath);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   rc = symlink(oldpath, (char*)-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);

   char linkval[PATH_MAX];
   memset(linkval, 0, PATH_MAX);
   rc = readlink(linkpath, linkval, PATH_MAX);
   ASSERT_NOT_EQ(-1, rc);
   rc = strcmp(linkval, oldpath);
   ASSERT_EQ(0, rc);
   rc = stat(linkpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));

   rc = readlink((char*)-1, linkval, PATH_MAX);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   rc = readlink(linkpath, (char*)-1, PATH_MAX);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);

   rc = unlink(linkpath);
   ASSERT_EQ(0, rc);
   rc = rmdir(oldpath);
   ASSERT_EQ(0, rc);

   rc = unlink((char*)-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);

   PASS();
}

TEST test_chdir()
{
   void* ptr;
   int rc;
   char oldpath[PATH_MAX];
   ptr = getcwd(oldpath, PATH_MAX);
   ASSERT_NOT_EQ(NULL, ptr);
   rc = chdir(dirpath);
   ASSERT_EQ(0, rc);
   char curpath[PATH_MAX];
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NOT_EQ(NULL, ptr);
   rc = strcmp(curpath, dirpath);
   ASSERT_EQ(0, rc);
   rc = chdir(oldpath);
   ASSERT_EQ(0, rc);
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NOT_EQ(NULL, ptr);
   rc = strcmp(curpath, oldpath);
   ASSERT_EQ(0, rc);

   int fd = open(dirpath, O_RDONLY);
   ASSERT_NOT_EQ(-1, fd);
   rc = fchdir(fd);
   ASSERT_EQ(0, rc);
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NOT_EQ(NULL, ptr);
   rc = strcmp(curpath, dirpath);
   ASSERT_EQ(0, rc);
   rc = chdir(oldpath);
   ASSERT_EQ(0, rc);
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NOT_EQ(NULL, ptr);
   rc = strcmp(curpath, oldpath);
   ASSERT_EQ(0, rc);

   ptr = getcwd((char*)-1, PATH_MAX);
   ASSERT_EQ(NULL, ptr);
   rc = chdir((char*)-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   PASS();
}

GREATEST_MAIN_DEFS();

char* cmdname = "?";

void usage()
{
   fprintf(stderr, "%s <directory>\n", cmdname);
}

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   cmdname = argv[0];
   if (argc < 2) {
      usage();
      return 1;
   }
   dirpath = argv[argc - 1];

   struct stat st;
   if (stat(dirpath, &st) < 0) {
      perror(dirpath);
      return 1;
   }
   if ((st.st_mode & S_IFMT) != S_IFDIR) {
      fprintf(stderr, "%s is not a directory\n", dirpath);
      return 1;
   }

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_mkdir);
   RUN_TEST(test_rename);
   RUN_TEST(test_symlink);
   RUN_TEST(test_chdir);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
