/*
 * Copyright 2023 Kontain Inc
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
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <linux/aio_abi.h>

/*
 * A simple test to exercise the io_*() system calls.
 * Usage:
 *   aio_test -f number_of_files -c number_of_iocontexts [-s]
 *
 * Env vars:
 *  AIO_TEST_WORKDIR - the path to an existing directory where the
 *     test files are created.  In addition, if the -s flag is used
 *     the waiting/continue files to are expected to be created there.
 *     Once the program is ready to be snapshotted it will creating
 *     the file "waiting" and will begin looking for the file "continue".
 *     Once the "continue" file appears, the program will remove both
 *     files and continue with the tests.  The "waiting" file is created
 *     after all test files and io contexts have been created and it is
 *     safe to request a snapshot.
 *     NOTE: this is not defining a protocol to be used for generating
 *     snapshots.  We do this to allow easy testing of a snapshot using
 *     io contexts.
 */

#define TEST_FILENAME "%s/aio_file_%d_%d"
#define WAITING_FILENAME "waiting"
#define CONTINUE_FILENAME "continue"

// We only support up to 222 concurrent io's in this test.
// Arbitrarily chosen.
#define MAX_FDS 222
// We write CHUNKSIZE bytes into each file for this test.
#define CHUNKSIZE 4096
// Upper limit on the number of io contexts this program will create
#define MAX_IOCONTEXTS 100
char* progname = "noname";
void usage(void)
{
   fprintf(stderr, "Usage: %s [-f number_of_files] [-c number_of_contexts] [-s]\n", progname);
   fprintf(stderr, "       -f - number_of_files can be 1 - %d, default is 100\n", MAX_FDS);
   fprintf(stderr, "       -c - number_of_contexts is limited to %d, default is 1\n", MAX_IOCONTEXTS);
   fprintf(stderr, "       -ss - pause to wait for a snapshot request, defaul to no pause\n");
   fprintf(stderr, "       -sf - pause to wait for a failing snapshot request, defaul to no pause\n");
}

char* workdir = NULL;

/*
 * Flag to indicate we should wait for a snapshot to be taken and where
 * we should pause.
 * 0 = don't pause for snapshot
 * 1 = pause for a successful snapshot
 * 2 = pause at a point where a failing snapshot should happen
 *     (there is active asynch i/o)
 */
int waitforsnap = 0;

// Helper routine to create a file signifying we are pausing for a snapshot
// to be generated and then we wait to be told to continue.
void pause_for_snapshot(void)
{
   char filename[128];
   struct stat statb;

   snprintf(filename, sizeof(filename), "%s/continue", workdir);
   unlink(filename);
   snprintf(filename, sizeof(filename), "%s/waiting", workdir);
   if (mknod(filename, S_IFREG | 0755, 0) != 0) {
      fprintf(stderr, "Unable to create %s file, %s\n", filename, strerror(errno));
      exit(1);
   }
   // wait to be told to continue
   snprintf(filename, sizeof(filename), "%s/continue", workdir);
   fprintf(stdout, "Waiting for file %s to exist\n", filename);
   while (stat(filename, &statb) < 0) {
      sleep(1);
   }
   fprintf(stdout, "Continuing\n");
   unlink(filename);
}

int io_submit_sync(aio_context_t iocontext,
                   int fdsync,
                   long nf,
                   int fdlist[],
                   struct iocb** iocblist,
                   struct io_event* eventlist)
{
   // Try out IOCB_CMD_FSYNC and IOCB_CMD_FDSYNC
   int rc;
   char* synctype = (fdsync != 0) ? "IOCB_CMD_FDSYNC" : "IOCB_CMD_FSYNC";
   for (int i = 0; i < nf; i++) {
      struct iocb* iocbp = iocblist[i];
      iocbp->aio_data = i;
      iocbp->aio_key = 0;
      iocbp->aio_rw_flags = 0;
      iocbp->aio_lio_opcode = (fdsync != 0) ? IOCB_CMD_FDSYNC : IOCB_CMD_FSYNC;
      iocbp->aio_reqprio = 0;
      iocbp->aio_fildes = fdlist[i];
      iocbp->aio_buf = 0;
      iocbp->aio_nbytes = 0;
      iocbp->aio_offset = 0;
      iocbp->aio_flags = 0;
      iocbp->aio_resfd = -1;
      iocbp->aio_reserved2 = 0;
   }
   rc = syscall(SYS_io_submit, iocontext, nf, iocblist);
   if (rc < 0) {
      fprintf(stderr, "SYS_io_submit for %s failed, %s\n", synctype, strerror(errno));
      return errno;
   }
   rc = syscall(SYS_io_getevents, iocontext, nf, nf, eventlist, NULL);
   if (rc < 0) {
      fprintf(stderr, "SYS_io_getevents %s failed, %s\n", synctype, strerror(errno));
      return errno;
   }
   fprintf(stdout, "io_getevents returned %d %s events\n", rc, synctype);
   return 0;
}

int io_submit_pwrite(aio_context_t iocontext,
                     long nf,
                     int fdlist[],
                     struct iocb** iocblist,
                     struct io_event* eventlist,
                     unsigned char* buffer,
                     int chunksize)
{
   long rc;

   // fill in write iocb's
   for (int i = 0; i < nf; i++) {
      struct iocb* iocbp = iocblist[i];
      iocbp->aio_data = i;
      iocbp->aio_rw_flags = 0;
      iocbp->aio_lio_opcode = IOCB_CMD_PWRITE;
      iocbp->aio_reqprio = 0;
      iocbp->aio_fildes = fdlist[i];
      iocbp->aio_buf = (__u64)&buffer[i * chunksize];
      iocbp->aio_nbytes = chunksize;
      iocbp->aio_offset = i * chunksize;
      iocbp->aio_flags = 0;
      iocbp->aio_resfd = -1;
      iocbp->aio_reserved2 = 0;
   }

   // start the i/o
   rc = syscall(SYS_io_submit, iocontext, nf, iocblist);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "SYS_io_submit for pwrite's failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "io_submit started %ld pwrite's\n", rc);

   if (waitforsnap == 2) {
      pause_for_snapshot();
   }

   // wait for io to complete
   rc = syscall(SYS_io_getevents, iocontext, nf, nf, eventlist, NULL);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "SYS_io_getevents failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "io_getevents returned %ld pwrite's events\n", rc);
   return 0;
}

int io_submit_pwritev(aio_context_t iocontext,
                      long nf,
                      int fdlist[],
                      struct iocb** iocblist,
                      struct io_event* eventlist,
                      unsigned char* buffer,
                      int chunksize)
{
   long rc;
   struct iovec iovec[MAX_FDS];

   // fill in write iocb's
   for (int i = 0; i < nf; i++) {
      struct iovec* iovecp = &iovec[i];
      iovecp->iov_base = &buffer[i * chunksize];
      iovecp->iov_len = chunksize;

      struct iocb* iocbp = iocblist[i];
      iocbp->aio_data = i;
      iocbp->aio_rw_flags = 0;
      iocbp->aio_lio_opcode = IOCB_CMD_PWRITEV;
      iocbp->aio_reqprio = 0;
      iocbp->aio_fildes = fdlist[i];
      iocbp->aio_buf = (__u64)iovecp;
      iocbp->aio_nbytes = 1;
      iocbp->aio_offset = i * chunksize;
      iocbp->aio_flags = 0;
      iocbp->aio_resfd = -1;
      iocbp->aio_reserved2 = 0;
   }

   // start the i/o
   rc = syscall(SYS_io_submit, iocontext, nf, iocblist);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "SYS_io_submit for pwritev's failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "io_submit started %ld pwritev's io's\n", rc);

   // wait for io to complete
   rc = syscall(SYS_io_getevents, iocontext, nf, nf, eventlist, NULL);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "SYS_io_getevents failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "io_getevents returned %ld pwritev events\n", rc);
   return 0;
}

int io_submit_pread(aio_context_t iocontext,
                    long nf,
                    int fdlist[],
                    struct iocb** iocblist,
                    struct io_event* eventlist,
                    unsigned char* buffer,
                    int chunksize)
{
   long rc;

   // setup iocb's to read data back into buffer
   for (int i = 0; i < nf; i++) {
      struct iocb* iocbp = iocblist[i];
      iocbp->aio_data = i;
      iocbp->aio_rw_flags = 0;
      iocbp->aio_lio_opcode = IOCB_CMD_PREAD;
      iocbp->aio_reqprio = 0;
      iocbp->aio_fildes = fdlist[i];
      iocbp->aio_buf = (__u64)&buffer[i * chunksize];
      iocbp->aio_nbytes = chunksize;
      iocbp->aio_offset = i * chunksize;
      iocbp->aio_flags = 0;
      iocbp->aio_resfd = -1;
      iocbp->aio_reserved2 = 0;
   }

   // start i/o to read data
   rc = syscall(SYS_io_submit, iocontext, nf, iocblist);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "io_submit for pread's failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "io_submit started %ld pread's\n", rc);

   // wait for reads to complete
   rc = syscall(SYS_io_getevents, iocontext, nf, nf, eventlist, NULL);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "io_getevent looking for active reads failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "%ld of %ld pread's have completed\n", rc, nf);
   return 0;
}

int io_submit_preadv(aio_context_t iocontext,
                     long nf,
                     int fdlist[],
                     struct iocb** iocblist,
                     struct io_event* eventlist,
                     unsigned char* buffer,
                     int chunksize)
{
   long rc;
   struct iovec iovec[MAX_FDS];

   // setup iocb's to read data back into buffer
   for (int i = 0; i < nf; i++) {
      struct iovec* iovecp = &iovec[i];
      iovecp->iov_base = &buffer[i * chunksize];
      iovecp->iov_len = chunksize;

      struct iocb* iocbp = iocblist[i];
      iocbp->aio_data = i;
      iocbp->aio_rw_flags = 0;
      iocbp->aio_lio_opcode = IOCB_CMD_PREADV;
      iocbp->aio_reqprio = 0;
      iocbp->aio_fildes = fdlist[i];
      iocbp->aio_buf = (__u64)iovecp;
      iocbp->aio_nbytes = 1;
      iocbp->aio_offset = i * chunksize;
      iocbp->aio_flags = 0;
      iocbp->aio_resfd = -1;
      iocbp->aio_reserved2 = 0;
   }

   // start i/o to read data
   rc = syscall(SYS_io_submit, iocontext, nf, iocblist);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "io_submit for preadv's failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "io_submit started %ld preadv's\n", rc);

   // wait for reads to complete
   rc = syscall(SYS_io_getevents, iocontext, nf, nf, eventlist, NULL);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "io_getevent looking for active preadv's failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "%ld of %ld preadv's have completed\n", rc, nf);
   return 0;
}

void print_io_events(long nf, struct io_event* eventlist)
{
   for (int i = 0; i < nf; i++) {
      fprintf(stdout,
              "io_event[%d]: data %llu, obj 0x%llx, res 0x%llx, res2 0x%llx\n",
              i,
              eventlist[i].data,
              eventlist[i].obj,
              eventlist[i].res,
              eventlist[i].res2);
   }
}

int io_submit_poll(aio_context_t iocontext,
                   long nf,
                   int* fdlist,
                   struct iocb** iocblist,
                   struct io_event* eventlist,
                   int pollflags)
{
   long rc;

   // setup iocb's to poll for fd readiness
   for (int i = 0; i < nf; i++) {
      struct iocb* iocbp = iocblist[i];
      iocbp->aio_data = i;
      iocbp->aio_rw_flags = 0;
      iocbp->aio_lio_opcode = IOCB_CMD_POLL;
      iocbp->aio_reqprio = 0;
      iocbp->aio_fildes = fdlist[i];
      iocbp->aio_buf = (__u64)pollflags;
      iocbp->aio_nbytes = 0;
      iocbp->aio_offset = 0;
      iocbp->aio_flags = 0;
      iocbp->aio_resfd = -1;
      iocbp->aio_reserved2 = 0;
   }
   rc = syscall(SYS_io_submit, iocontext, nf, iocblist);
   if (rc < 0) {
      rc = errno;
      return rc;
   }
   fprintf(stdout, "io_submit started %ld poll's\n", rc);

   // wait for polls to complete
   rc = syscall(SYS_io_getevents, iocontext, nf, nf, eventlist, NULL);
   if (rc < 0) {
      rc = errno;
      fprintf(stderr, "io_getevent looking for active poll's failed, %s\n", strerror(errno));
      return rc;
   }
   fprintf(stdout, "%ld of %ld poll's have completed\n", rc, nf);
   return 0;
}

int verify_buffer_contents(long nf, unsigned char* buffer, int chunksize)
{
   int miscompares = 0;
   // verify buffer contents
   fprintf(stdout, "Verify %ld buffer contents\n", nf);
   for (int i = 0; i < nf; i++) {
      for (int j = 0; j < chunksize; j++) {
         if (buffer[i * chunksize + j] != (unsigned char)i) {
            fprintf(stderr,
                    "fd %d, offset %d, should contain %d but contains %u\n",
                    i,
                    j,
                    i,
                    buffer[i * chunksize + j]);
            miscompares++;
         }
      }
   }
   fprintf(stdout, "Found %d miscompares\n", miscompares);
   return miscompares;
}

int do_iocontext_test(aio_context_t iocontext,
                      int nf,
                      int* fdlist,
                      struct iocb** iocblist,
                      struct io_event* eventlist,
                      unsigned char* buffer,
                      int chunksize)
{
   int rc;

   fprintf(stdout, "\n\n**** Begin test with iocontext 0x%lx\n", iocontext);

   // Try out IOCB_CMD_PWRITE
   if ((rc = io_submit_pwrite(iocontext, nf, fdlist, iocblist, eventlist, buffer, chunksize)) != 0) {
      goto done;
   }

   // Try out IOCB_CMD_FDSYNC
   if ((rc = io_submit_sync(iocontext, 1, nf, fdlist, iocblist, eventlist)) != 0) {
      goto done;
   }
   // Try out IOCB_CMD_FSYNC
   if ((rc = io_submit_sync(iocontext, 0, nf, fdlist, iocblist, eventlist)) != 0) {
      goto done;
   }

   // Clear our buffer before reading into it.
   memset(buffer, 0xff, nf * chunksize);

   if ((rc = io_submit_pread(iocontext, nf, fdlist, iocblist, eventlist, buffer, chunksize)) != 0) {
      goto done;
   }
   // Verify buffer contents
   if (verify_buffer_contents(nf, buffer, chunksize) != 0) {
      rc = EINVAL;
      goto done;
   }

   // Truncate the test files.
   fprintf(stdout, "Truncate test files before writing\n");
   for (int i = 0; i < nf; i++) {
      if (ftruncate(fdlist[i], 0) < 0) {
         rc = errno;
         fprintf(stderr, "Couldn't truncate fd %d, %s\n", fdlist[i], strerror(errno));
         goto done;
      }
   }

   // We don't reinitialize the buffer, we do depend on the values read in the preceding
   // part of the test.  If you rearrange this code you may need to reinit the buffers.

   // Try IOCB_CMD_PWRITEV
   if ((rc = io_submit_pwritev(iocontext, nf, fdlist, iocblist, eventlist, buffer, chunksize)) != 0) {
      goto done;
   }

   // Clear our buffer before reading into it.
   memset(buffer, 0xff, nf * chunksize);

   // Try IOCB_CMD_PREADV
   if ((rc = io_submit_preadv(iocontext, nf, fdlist, iocblist, eventlist, buffer, chunksize)) != 0) {
      goto done;
   }
   // Verify buffer contents
   if (verify_buffer_contents(nf, buffer, chunksize) != 0) {
      rc = EINVAL;
      goto done;
   }

   // Try IOCB_CMD_POLL
   int pollflags = POLLIN | POLLOUT;
   if ((rc = io_submit_poll(iocontext, nf, fdlist, iocblist, eventlist, pollflags)) != 0) {
      goto done;
   }
   print_io_events(nf, eventlist);
   for (int i = 0; i < nf; i++) {
      if (eventlist[i].res != pollflags) {
         fprintf(stderr,
                 "Unexpected poll event flags for file %d, got 0x%llx, expected 0x%x\n",
                 i,
                 eventlist[i].res,
                 pollflags);
         goto done;
      }
   }
   fprintf(stdout, "asynch poll returned expected results\n");

   fprintf(stdout, "**** test with iocontext 0x%lx succeeded\n", iocontext);
done:;
   return rc;
}

int main(int argc, char* argv[])
{
   int nf = 100;
   int nc = 1;
   long rc = 0;
   int chunksize = CHUNKSIZE;
   unsigned char buffer[MAX_FDS * CHUNKSIZE];
   int fdlist[MAX_FDS];
   struct iocb iocb[MAX_FDS];
   struct iocb* iocblist[MAX_FDS];
   struct io_event eventlist[MAX_FDS];
   aio_context_t iocontext[MAX_IOCONTEXTS];
   char filename[128];
   struct stat statb;

   progname = argv[0];

   // Check for a workdir
   workdir = getenv("AIO_TEST_WORKDIR");
   if (workdir == NULL) {
      workdir = "/tmp";
   }

   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-f") == 0) {
         nf = atoi(argv[i + 1]);
         if (nf <= 0 || nf > MAX_FDS) {
            usage();
            return 1;
         }
         i++;
      } else if (strcmp(argv[i], "-c") == 0) {
         nc = atoi(argv[i + 1]);
         if (nc < 0 || nc > MAX_IOCONTEXTS) {
            usage();
            return 1;
         }
         i++;
      } else if (strcmp(argv[i], "-ss") == 0) {
         waitforsnap = 1;
         snprintf(filename, sizeof(filename), "%s/waiting", workdir);
         if (stat(filename, &statb) == 0) {
            fprintf(stderr, "Flag file %s must not exist before the test is started\n", filename);
            return 1;
         }
      } else if (strcmp(argv[i], "-sf") == 0) {
         waitforsnap = 2;
         snprintf(filename, sizeof(filename), "%s/waiting", workdir);
         if (stat(filename, &statb) == 0) {
            fprintf(stderr, "Flag file %s must not exist before the test is started\n", filename);
            return 1;
         }
      } else {
         fprintf(stderr, "Unknown command line flag %s\n", argv[i]);
         usage();
         return 1;
      }
   }
   fprintf(stdout, "Using %d files and %d io contexts for this test run\n", nf, nc);

   if (stat(workdir, &statb) != 0) {
      fprintf(stderr, "workdir %s must exist\n", workdir);
      return 1;
   }
   fprintf(stdout, "Using %s as our work directory\n", workdir);

   // create data for test file contents and open test files
   for (int i = 0; i < nf; i++) {
      // fill buffer
      memset(&buffer[i * chunksize], i, chunksize);

      // open files, pid may not be unique in a container
      snprintf(filename, sizeof(filename), TEST_FILENAME, workdir, getpid(), i);
      fdlist[i] = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0777);
      if (fdlist[i] < 0) {
         fprintf(stderr, "Can't create %s, %s\n", filename, strerror(errno));
         goto done;
      }

      iocblist[i] = &iocb[i];
   }

   // create io contexts.
   for (int i = 0; i < nc; i++) {
      rc = syscall(SYS_io_setup, 2 * nf, &iocontext[i]);
      if (rc != 0) {
         rc = errno;
         fprintf(stderr, "SYS_io_setup for context #%d failed, %s\n", i, strerror(errno));
         goto done;
      }
      fprintf(stdout, "IO Context number %d: 0x%lx created\n", i, iocontext[i]);
   }

   // If requested pause to allow a snapshot to be taken
   if (waitforsnap == 1) {
      pause_for_snapshot();
   }

   for (int i = 0; i < nc; i++) {
      if (do_iocontext_test(iocontext[i], nf, fdlist, iocblist, eventlist, buffer, chunksize) != 0) {
         fprintf(stderr, "Test %d for iocontext 0x%lx failed\n", i, iocontext[i]);
         goto done;
      }
   }

   // Try io_cancel()  (someday)
   // We need to find a very slow disk type device or swamp a fast one so that
   // requests are delayed long enough for us to cancel them.  One wonders how we
   // will be able to test this in ci running in the cloud where latency is all
   // over the map.
   fprintf(stdout, "***** io_cancel() is not being tested for now *****\n");

   // destroy io context
   for (int i = 0; i < nc; i++) {
      rc = syscall(SYS_io_destroy, iocontext[i]);
      if (rc < 0) {
         rc = errno;
         fprintf(stderr, "io_destroy failed, %s\n", strerror(errno));
         goto done;
      }
      fprintf(stdout, "IO Context %d:  0x%lx destroyed\n", i, iocontext[i]);
   }

   fprintf(stdout, "Test completed with no errors\n");

done:;
   // close and delete test files
   for (int i = 0; i < nf; i++) {
      if (fdlist[i] < 0) {
         // If we didn't even finish opening files, stop where we failed.
         break;
      }
      close(fdlist[i]);
      snprintf(filename, sizeof(filename), TEST_FILENAME, workdir, getpid(), i);
      unlink(filename);
   }

   return rc;
}
