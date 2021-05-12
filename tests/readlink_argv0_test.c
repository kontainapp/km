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
 *
 * Tests for file system oriented operations.
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
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>

#ifndef STATX_ALL
#include <linux/stat.h>
#endif

#include "syscall.h"

#include "greatest/greatest.h"

char* argv0;

TEST test_readlink_argv0()
{
   char slink[128];
   int rc = readlink(argv0, slink, sizeof(slink));
   ASSERT_NEQm("Need to be called via symlink to make sense", -1, rc);
   slink[rc] = '\0';
   printf("slink=%s\n", slink);
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
   argv0 = argv[0];
   GREATEST_MAIN_BEGIN();

   greatest_set_verbosity(1);   // needed if we want to pass through | greatest/contrib/greenest,
                                // especially from KM payload
   RUN_TEST(test_readlink_argv0);

   GREATEST_PRINT_REPORT();
   exit(greatest_info.failed);   // return count of errors (or 0 if all is good)
}
