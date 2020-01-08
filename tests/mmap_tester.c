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
 * Common code for guest memory management (mmap/mprotect/madvise/munmap) testing
 */
#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include "syscall.h"

#include "greatest/greatest.h"
#include "mmap_test.h"

int fail = 0;

void sig_handler(int signal)
{
   if (signal != SIGSEGV) {
      warn("Unexpected signal caught: %d", signal);
      fail = 1;
   }
   siglongjmp(jbuf, SIGSEGV);
}
int mmap_test(mmap_test_t* tests)
{
   int ret;
   void* remapped_addr;            // address mremap uses (usually last_addr + offset)
   void* new_addr = MAP_FAILED;    // address mremap on map returns.
   void* last_addr = MAP_FAILED;   // changed by successful mmap

   for (mmap_test_t* t = tests; t->info != NULL; t++) {
      errno = 0;
      if (greatest_get_verbosity() == 1) {
         printf("* %s: last_addr %p, offset 0x%lx (%s) size 0x%lx (%s)\n",
                t->info,
                last_addr,
                t->offset,
                out_sz(t->offset),
                t->size,
                out_sz(t->size));
      }
      switch (t->type) {
         case TYPE_MMAP:
            new_addr = mmap((void*)t->offset, t->size, t->prot, t->flags, -1, 0);
            if (greatest_get_verbosity() == 1) {
               printf("return: %p (%s)\n", new_addr, out_sz((uint64_t)new_addr));
            }
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);   // print errno out if test fails
               ASSERT_NOT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               if ((t->prot & PROT_READ) != 0) {
                  ASSERT_EQm("Mmaped memory should be zeroed", 0, *(int*)new_addr);
               }
               if ((t->prot & PROT_WRITE) != 0) {
                  if (greatest_get_verbosity() == 1) {
                     printf("Mmap OK, trying to memset '2' to 0x%lx size: 0x%lx (%s)\n",
                            (uint64_t)new_addr,
                            t->size,
                            out_sz(t->size));
                  }
                  memset(new_addr, '2', t->size);
               }
               last_addr = new_addr;
            } else {
               ASSERT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            get_maps(greatest_get_verbosity());
            break;
         case TYPE_MUNMAP:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            ret = munmap(last_addr + t->offset, t->size);
            get_maps(greatest_get_verbosity());
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            break;
         case TYPE_MREMAP:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            remapped_addr = last_addr + t->offset;
            size_t old_size = t->size;
            size_t new_size = t->prot;
            new_addr = mremap(remapped_addr, old_size, new_size, t->flags);
            get_maps(greatest_get_verbosity());
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);   // print errno out if test fails
               ASSERT_NOT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               if (greatest_get_verbosity() == 1) {
                  printf("mremap OK %p -> %p, will memset '2' size old 0x%lx (%s) new 0x%lx (%s)\n",
                         last_addr,
                         new_addr,
                         old_size,
                         out_sz(old_size),
                         new_size,
                         out_sz(new_size));
               }
               if (old_size < new_size) {   // WE ASSUME PROT_READ for the parent map !
                  printf("VALUE %d at %p\n", *(int*)(new_addr + old_size), new_addr);
                  ASSERT_EQm("new range in remap should be zeroed", 0, *(int*)(new_addr + old_size));
               }
               memset(new_addr, '2', new_size);   // just core dumps if something is wrong
               signal(SIGSEGV, sig_handler);
               if ((ret = sigsetjmp(jbuf, 1)) == 0) {
                  if (new_addr != remapped_addr) {   // old memory should be not accessible now
                     memset(remapped_addr, '2', old_size);
                     FAILm("memset to new address is successful and should be not");
                  } else if (old_size > new_size) {   // shrinking. Extra should be unmapped by now
                     ASSERT_EQ(new_addr, remapped_addr);
                     void* unmapped_addr = remapped_addr + new_size;
                     size_t unmapped_size = old_size - new_size;
                     memset(unmapped_addr, '2', unmapped_size);
                     printf("memset to removed %p size 0x%lx (%s) should have failed but did not\n",
                            unmapped_addr,
                            unmapped_size,
                            out_sz(unmapped_size));
                     FAIL();
                  }
               } else {
                  assert(ret == SIGSEGV);   // we use that value in longjmp
               }
               signal(t->expected, SIG_DFL);
            } else {   // expecting failure
               ASSERT_EQ_FMTm(t->info, MAP_FAILED, new_addr, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            break;
         case TYPE_USE_MREMAP_ADDR:
            assert(new_addr != MAP_FAILED);
            last_addr = new_addr;
            break;
         case TYPE_MPROTECT:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            ret = mprotect(last_addr + t->offset, t->size, t->prot);
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            get_maps(greatest_get_verbosity());
            break;
         case TYPE_WRITE:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected == OK) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               break;
            }
            signal(t->expected, sig_handler);
            if ((ret = sigsetjmp(jbuf, 1)) == 0) {
               memset(last_addr + t->offset, (char)t->prot, t->size);
               printf("Write to %p (sz 0x%lx) was successful and should be not (line %d)\n",
                      last_addr + t->offset,
                      t->size,
                      t->line);
               FAIL();
            } else {
               assert(ret == SIGSEGV);   // we use that value in longjmp
            }
            signal(t->expected, SIG_DFL);
            ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            break;
         case TYPE_READ:
            assert(last_addr != MAP_FAILED);   // we should have failed test already
            if (t->expected == OK) {
               for (size_t i = 0; i < t->size; i++) {
                  volatile char c = *(char*)(last_addr + t->offset + i);
                  if (t->prot == 0) {   // if t->prot 0 expect 0's, else expect c to equal
                                        // t->prot to verify proper read in
                     ASSERT_EQm("read in not 0's even though it should be", c, t->prot);
                  } else {
                     ASSERT_EQm("t->prot should equal c", c, (char)t->prot);
                  }

                  assert(c != c + 1);   // stop gcc from complaining,but generate code
               }

               break;
            }
            signal(t->expected, sig_handler);
            if ((ret = sigsetjmp(jbuf, 1)) == 0) {
               for (size_t i = 0; i < t->size; i++) {
                  volatile char c = *(char*)(last_addr + t->offset + i);
                  assert(c != c + 1);   // stop gcc from complaining,but generate code
               }
               FAILm("Read successful and should be not");   // return
               assert(ret == SIGSEGV);                       // we use that value in longjmp
               signal(t->expected, SIG_DFL);
               ASSERT_EQm("signal handler caught unexpected signal", 0, fail);
            }
            break;
         case TYPE_MADVISE:
            assert(last_addr != MAP_FAILED);

            ret = madvise(last_addr + t->offset, t->size, t->advise);
            if (t->expected == OK) {
               ASSERT_EQ_FMTm(t->info, 0, errno, errno_fmt);
               ASSERT_EQ_FMTm(t->info, 0, ret, ret_fmt);
            } else {
               ASSERT_NOT_EQ_FMTm(t->info, 0, ret, ret_fmt);
               ASSERT_EQ_FMTm(t->info, t->expected, errno, errno_fmt);
            }
            break;
         default:
            ASSERT_EQ(NULL, "Not reachable");
      }   // switch
   }      // for
   PASS();
}