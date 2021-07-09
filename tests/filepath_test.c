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
 * Tests for file path oriented system calls
 */

#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
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
   ASSERT_NEQ(-1, rc);
   rc = strcmp(linkval, oldpath);
   ASSERT_EQ(0, rc);
   rc = stat(linkpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));

   memset(linkval, 0, PATH_MAX);
   rc = readlinkat(AT_FDCWD, linkpath, linkval, PATH_MAX);
   ASSERT_NEQ(-1, rc);
   rc = strcmp(linkval, oldpath);
   printf("rc=%d\n", rc);
   ASSERT_EQ(0, rc);
   rc = stat(linkpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));

   memset(linkval, 0, PATH_MAX);
   int fd = open(".", O_RDONLY);
   rc = readlinkat(fd, linkpath, linkval, PATH_MAX);
   ASSERT_NEQ(-1, rc);
   rc = strcmp(linkval, oldpath);
   ASSERT_EQ(0, rc);
   rc = stat(linkpath, &st);
   ASSERT_EQ(0, rc);
   ASSERT_EQ(S_IFDIR, (st.st_mode & S_IFMT));
   close(fd);

   rc = readlink((char*)-1, linkval, PATH_MAX);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   char* fake_buf = (char*)-1;   // avoid gcc-11 overflow check via a temp var
   rc = readlink(linkpath, fake_buf, PATH_MAX);
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
   ASSERT_NEQ(NULL, ptr);
   rc = chdir(dirpath);
   ASSERT_EQ(0, rc);
   char curpath[PATH_MAX];
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NEQ(NULL, ptr);
   rc = strcmp(curpath, dirpath);
   ASSERT_EQ(0, rc);
   rc = chdir(oldpath);
   ASSERT_EQ(0, rc);
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NEQ(NULL, ptr);
   rc = strcmp(curpath, oldpath);
   ASSERT_EQ(0, rc);

   int fd = open(dirpath, O_RDONLY);
   ASSERT_NEQ(-1, fd);
   rc = fchdir(fd);
   ASSERT_EQ(0, rc);
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NEQ(NULL, ptr);
   rc = strcmp(curpath, dirpath);
   ASSERT_EQ(0, rc);
   rc = chdir(oldpath);
   ASSERT_EQ(0, rc);
   ptr = getcwd(curpath, PATH_MAX);
   ASSERT_NEQ(NULL, ptr);
   rc = strcmp(curpath, oldpath);
   ASSERT_EQ(0, rc);

   char* fake_buf = (char*)-1;   // avoid gcc-11 overflow check via a temp var
   ptr = getcwd(fake_buf, PATH_MAX);
   ASSERT_EQ(NULL, ptr);
   rc = chdir((char*)-1);
   ASSERT_EQ(-1, rc);
   ASSERT_EQ(EFAULT, errno);
   PASS();
}

int nloops = 10000;
/*
 * Concurrent open test. This is excerises KM file descript table locking.
 */
#define OPEN_TEST_NFILES 20
struct open_task_file {
   char path[PATH_MAX];
   struct stat st;
};
struct open_task_file open_task_files[OPEN_TEST_NFILES];

#define OPEN_TEST_NTASK 20

pthread_mutex_t open_test_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t open_test_cond = PTHREAD_COND_INITIALIZER;
int open_test_start = 0;

void* open_task_main(void* arg)
{
   int* statusp = arg;
   *statusp = 0;

   // Synchronized startup
   pthread_mutex_lock(&open_test_mutex);
   while (open_test_start == 0) {
      pthread_cond_wait(&open_test_cond, &open_test_mutex);
   }
   pthread_mutex_unlock(&open_test_mutex);

   for (int i = 0; i < nloops; i++) {
      for (int j = 0; j < OPEN_TEST_NFILES; j++) {
         int fd = open(open_task_files[j].path, O_RDONLY);
         if (fd < 0) {
            perror(open_task_files[j].path);
            continue;
         }
         struct stat st;
         if (fstat(fd, &st) < 0) {
            fprintf(stderr, "fstat failure: %s %d\n", open_task_files[j].path, errno);
            *statusp = *statusp + 1;
         }
         if (st.st_ino != open_task_files[j].st.st_ino) {
            fprintf(stderr, "ino mismatch %s\n", open_task_files[j].path);
            *statusp = *statusp + 1;
         }
         close(fd);
      }
   }
   return NULL;
}

TEST test_concurrent_open()
{
   int rc;
   pthread_t thread[OPEN_TEST_NTASK];
   int open_task_status[OPEN_TEST_NTASK];

   for (int i = 0; i < OPEN_TEST_NFILES; i++) {
      snprintf(open_task_files[i].path, PATH_MAX, "%s/file%d", dirpath, i);
      rc = mknod(open_task_files[i].path, S_IFREG | S_IRUSR, 0);
      ASSERT_EQ(0, rc);
      rc = stat(open_task_files[i].path, &open_task_files[i].st);
      ASSERT_EQ(0, rc);
   }

   for (int i = 0; i < OPEN_TEST_NTASK; i++) {
      rc = pthread_create(&thread[i], 0, open_task_main, &open_task_status[i]);
      ASSERT_EQ(0, rc);
   }

   // release the hounds...
   pthread_mutex_lock(&open_test_mutex);
   open_test_start = 1;
   pthread_cond_broadcast(&open_test_cond);
   pthread_mutex_unlock(&open_test_mutex);

   for (int i = 0; i < OPEN_TEST_NTASK; i++) {
      void* rval;
      rc = pthread_join(thread[i], &rval);
      ASSERT_EQ(0, rc);
   }
   for (int i = 0; i < OPEN_TEST_NFILES; i++) {
      rc = unlink(open_task_files[i].path);
   }
   for (int i = 0; i < OPEN_TEST_NFILES; i++) {
      ASSERT_EQ(0, open_task_status[i]);
   }
   PASS();
}

TEST test_unlinkat(void)
{
   char path[PATH_MAX];
   long unique;
   struct timespec ts;
   int rc;
   char* pathoftmp = dirpath;
   int tmpfd;
   int fd;

   // Since getpid() just returns 1 mostly, make a number that is probably unique enough for this test
   clock_gettime(CLOCK_MONOTONIC, &ts);
   unique = (ts.tv_sec << 32) | ts.tv_nsec;

   // Cleanup files that would interfere with the test, we expect these to fail
   snprintf(path, sizeof(path), "%s/file_%ld", pathoftmp, unique);
   unlink(path);
   snprintf(path, sizeof(path), "%s/dir_%ld", pathoftmp, unique);
   rmdir(path);
   snprintf(path, sizeof(path), "file_%ld", unique);
   unlink(path);
   snprintf(path, sizeof(path), "dir_%ld", unique);
   rmdir(path);

   // Create and unlink a file by absolute path.
   snprintf(path, sizeof(path), "%s/file_%ld", pathoftmp, unique);
   fd = open(path, O_CREAT, 0644);
   ASSERT_NEQ(-1, fd);
   close(fd);
   rc = unlinkat(0, path, 0);   // dirfd should be ignored when path is absolute
   ASSERT_EQ(0, rc);
   fd = open(path, O_CREAT, 0x644);
   ASSERT_NEQ(-1, fd);
   close(fd);
   rc = unlinkat(AT_FDCWD, path, 0);   // AT_FDCWD should ignored when path is absolute
   ASSERT_EQ(0, rc);

   // Create and unlink a directory by absolute path..
   snprintf(path, sizeof(path), "%s/dir_%ld", pathoftmp, unique);
   rc = mkdir(path, 0x644);
   ASSERT_EQ(0, rc);
   rc = unlinkat(0, path, 0);   // ignore dirfd, can't unlink dir without AT_REMOVEDIR
   ASSERT_EQ(-1, rc);
   rc = unlinkat(0, path, AT_REMOVEDIR);   // dirfd should be ignored and unlinking the dir should work
   ASSERT_EQ(0, rc);

   rc = mkdir(path, 0x644);
   ASSERT_EQ(0, rc);
   rc = unlinkat(AT_FDCWD, path, 0);   // can't unlink dir without AT_REMOVEDIR
   ASSERT_EQ(-1, rc);
   rc = unlinkat(AT_FDCWD, path, AT_REMOVEDIR);   // should be able to unlink dir with AT_REMOVEDIR
   ASSERT_EQ(0, rc);

   // Now do stuff relative to a directory fd.
   tmpfd = open(pathoftmp, O_DIRECTORY);
   ASSERT_NEQ(-1, tmpfd);

   // Create and unlink a file relative to an fd open on a directory
   snprintf(path, sizeof(path), "file_%ld", unique);
   fd = openat(tmpfd, path, O_CREAT, 0644);
   ASSERT_NEQ(-1, fd);
   close(fd);
   rc = unlinkat(tmpfd, path, AT_REMOVEDIR);   // should fail since this is a file
   ASSERT_EQ(-1, rc);
   rc = unlinkat(tmpfd, path, 0);
   ASSERT_EQ(0, rc);

   // Create and unlink a directory relative to an fd
   snprintf(path, sizeof(path), "%s/dir_%ld", pathoftmp, unique);
   rc = mkdir(path, 0755);
   ASSERT_EQ(0, rc);
   snprintf(path, sizeof(path), "dir_%ld", unique);
   rc = unlinkat(tmpfd, path, 0);   // should fail without AT_REMOVEDIR
   ASSERT_EQ(-1, rc);
   rc = unlinkat(tmpfd, path, AT_REMOVEDIR);
   ASSERT_EQ(0, rc);

   close(tmpfd);

   // Now do stuff in the current directory
   snprintf(path, sizeof(path), "file_%ld", unique);
   fd = openat(AT_FDCWD, path, O_CREAT, 0644);
   ASSERT_NEQ(-1, fd);
   close(fd);
   rc = unlinkat(AT_FDCWD, path, AT_REMOVEDIR);   // not a directory, should fail
   ASSERT_EQ(-1, rc);
   rc = unlinkat(AT_FDCWD, path, 0);
   ASSERT_EQ(0, rc);

   snprintf(path, sizeof(path), "dir_%ld", unique);
   rc = mkdir(path, 0755);
   ASSERT_EQ(0, rc);
   rc = unlinkat(AT_FDCWD, path, 0);   // should fail because this is a directory
   ASSERT_EQ(-1, rc);
   rc = unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
   ASSERT_EQ(0, rc);

   PASS();
}

GREATEST_MAIN_DEFS();

char* cmdname = "?";

void usage()
{
   fprintf(stderr, "%s <directory> [<loops>]\n", cmdname);
}

int main(int argc, char** argv)
{
   GREATEST_MAIN_BEGIN();
   cmdname = argv[0];
   if (argc < 2) {
      usage();
      return 1;
   }
   dirpath = argv[1];
   if (argc == 3) {
      char* ep = NULL;
      int val = strtol(argv[2], &ep, 0);
      if (*ep != '\0') {
         usage();
         return 1;
      }
      nloops = val;
   } else if (argc > 3) {
      usage();
      return 1;
   }

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
   RUN_TEST(test_concurrent_open);
   RUN_TEST(test_unlinkat);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
