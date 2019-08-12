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
   char newpath[PATH_MAX];
   snprintf(newpath, PATH_MAX, "%s/mydir", dirpath);
   ASSERT_EQ(0, mkdir(newpath, 0777));
   struct stat st;
   ASSERT_EQ(0, stat(newpath, &st));
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   ASSERT_EQ(0, rmdir(newpath));
   PASS();
}

TEST test_rename()
{
   char oldpath[PATH_MAX];
   snprintf(oldpath, PATH_MAX, "%s/mydir", dirpath);
   ASSERT_EQ(0, mkdir(oldpath, 0777));
   struct stat st;
   ASSERT_EQ(0, stat(oldpath, &st));
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   char newpath[PATH_MAX];
   snprintf(newpath, PATH_MAX, "%s/mydir2", dirpath);
   ASSERT_EQ(0, rename(oldpath, newpath));
   ASSERT_EQ(-1, stat(oldpath, &st));
   ASSERT_EQ(ENOENT, errno);
   ASSERT_EQ(0, stat(newpath, &st));
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   ASSERT_EQ(0, rmdir(newpath));
   PASS();
}

TEST test_symlink()
{
   char oldpath[PATH_MAX];
   snprintf(oldpath, PATH_MAX, "%s/mydir", dirpath);
   ASSERT_EQ(0, mkdir(oldpath, 0777));

   char linkpath[PATH_MAX];
   snprintf(linkpath, PATH_MAX, "%s/mydir-link", dirpath);
   ASSERT_EQ(0, symlink(oldpath, linkpath));
   struct stat st;
   ASSERT_EQ(0, lstat(linkpath, &st));
   ASSERT_EQ(S_IFLNK, (st.st_mode & S_IFMT));

   char linkval[PATH_MAX];
   memset(linkval, 0, PATH_MAX);
   ASSERT_NOT_EQ(-1, readlink(linkpath, linkval, PATH_MAX));
   ASSERT_EQ(0, strcmp(linkval, oldpath));
   ASSERT_EQ(0, stat(linkpath, &st));
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   ASSERT_EQ(0, unlink(linkpath));
   ASSERT_EQ(0, rmdir(oldpath));

   PASS();
}

TEST test_chdir()
{
   char oldpath[PATH_MAX];
   ASSERT_NOT_EQ(NULL, getcwd(oldpath, PATH_MAX));
   ASSERT_EQ(0, chdir(dirpath));
   char curpath[PATH_MAX];
   ASSERT_NOT_EQ(NULL, getcwd(curpath, PATH_MAX));
   ASSERT_EQ(0, strcmp(curpath, dirpath));
   ASSERT_EQ(0, chdir(oldpath));
   ASSERT_NOT_EQ(NULL, getcwd(curpath, PATH_MAX));
   ASSERT_EQ(0, strcmp(curpath, oldpath));

   int fd = open(dirpath, O_RDONLY);
   ASSERT_NOT_EQ(-1, fd);
   ASSERT_EQ(0, fchdir(fd));
   ASSERT_NOT_EQ(NULL, getcwd(curpath, PATH_MAX));
   ASSERT_EQ(0, strcmp(curpath, dirpath));
   ASSERT_EQ(0, chdir(oldpath));
   ASSERT_NOT_EQ(NULL, getcwd(curpath, PATH_MAX));
   ASSERT_EQ(0, strcmp(curpath, oldpath));
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
   if (argc != 2) {
      usage();
      return 1;
   }
   dirpath = argv[1];

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
