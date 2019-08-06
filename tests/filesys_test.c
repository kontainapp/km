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
 * This tests pathname translation between a guest payload and the host.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/stat.h>

#include "greatest/greatest.h"

struct test_scenario {
   char* path;   // file path name
   int exists;
} case_files[] = {
    {.path = "../bats", .exists = 0},
    {.path = "./../bats", .exists = 0},
    {.path = "../visible", .exists = 1},
    {.path = "/lib64", .exists = 0},
    {.path = "/visible", .exists = 1},
    {},
};

TEST test_stat()
{
   struct stat st;

   for (int i = 0; case_files[i].path != NULL; i++) {
      ASSERT_EQ((case_files[i].exists != 0) ? 0 : -1, stat(case_files[i].path, &st));
   }
   PASS();
}
TEST test_statx()
{
   struct statx stx;

   for (int i = 0; case_files[i].path != NULL; i++) {
      ASSERT_EQ((case_files[i].exists != 0) ? 0 : -1,
                syscall(SYS_statx, AT_FDCWD, case_files[i].path, 0, STATX_ALL, &stx));
   }
   PASS();
}

TEST test_mkdir()
{
   struct stat st;
   ASSERT_EQ(0, mkdir("../newdir", 0777));   // we can't .. past virtual root.
   ASSERT_EQ(0, stat("newdir", &st));
   ASSERT_EQ(S_IFDIR, st.st_mode & S_IFDIR);

   ASSERT_EQ(0, stat("newdir/../../newdir", &st));
   ASSERT_EQ(S_IFDIR, st.st_mode & S_IFDIR);

   PASS();
}

TEST test_symlink()
{
   struct stat st;
   ASSERT_EQ(0, symlink("visible", "../visible-link"));
   ASSERT_EQ(0, lstat("visible-link", &st));
   ASSERT_EQ(S_IFLNK, st.st_mode & S_IFMT);
   ASSERT_EQ(0, stat("visible-link", &st));
   ASSERT_EQ(S_IFREG, st.st_mode & S_IFMT);

   char buf[PATH_MAX];
   ASSERT_NOT_EQ(-1, readlink("/visible-link", buf, sizeof(buf)));
   ASSERT_EQ(0, strncmp("visible", buf, sizeof(buf)));
   PASS();
}

TEST test_rename()
{
   struct stat st;
   ASSERT_EQ(0, stat("visible", &st));

   ASSERT_EQ(0, rename("visible", ".visible"));
   ASSERT_EQ(-1, stat("visible", &st));
   fprintf(stderr, "errno=%d\n", errno);
   ASSERT_EQ(ENOENT, errno);
   ASSERT_EQ(0, stat(".visible", &st));

   ASSERT_EQ(0, rename("../.visible", "visible"));
   ASSERT_EQ(0, stat("visible", &st));

   PASS();
}

/*
 * create a directory in root
 *
 * 1) chdir to it
 * 2) chdir back to root,
 * 3 rmdir the directory
 */
TEST test_chdir()
{
   ASSERT_EQ(0, mkdir("/mydir", 0777));

   char buf[PATH_MAX];

   ASSERT_NOT_EQ(0, getcwd(buf, PATH_MAX));
   ASSERT_EQ(0, strcmp("/", buf));

   ASSERT_EQ(0, chdir("/mydir"));
   ASSERT_NOT_EQ(0, getcwd(buf, PATH_MAX));
   ASSERT_EQ(0, strcmp("/mydir", buf));

   ASSERT_EQ(0, chdir("/"));
   ASSERT_NOT_EQ(0, getcwd(buf, PATH_MAX));
   ASSERT_EQ(0, strcmp("/", buf));

   // Absolute path into chdir for now
   ASSERT_EQ(-1, chdir("mydir"));
   ASSERT_EQ(EINVAL, errno);

   ASSERT_EQ(0, rmdir("/mydir"));

   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   warnx("** THIS TEST REQUIRES A DIRECTORY STRUCTURE CREATED BY km_core_tests.bat");
   warnx("** IT WILL LIKE BREAK IF RUN STANDALONE ***");

   /* Tests can  be run as suites, or directly. Lets run directly. */
   RUN_TEST(test_stat);
   RUN_TEST(test_statx);
   RUN_TEST(test_mkdir);
   // RUN_TEST(test_symlink);
   RUN_TEST(test_rename);
   RUN_TEST(test_chdir);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
