#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/aio_abi.h>

/*
 * A simple test to exercise the io_*() system calls.
 * Usage:
 *   aio_test number_of_files
 */

#define TEST_FILENAME "/tmp/aio_file_%d_%d"

// We only support up to 222 concurrent io's in this test.
// Arbitrarily chosen.
#define MAX_FDS 222
// We write CHUNKSIZE bytes into each file for this test.
#define CHUNKSIZE 4096
char* progname = "noname";
void usage(void)
{
   fprintf(stderr, "Usage: %s number_of_files\n", progname);
   fprintf(stderr, "       number_of_files can be 1 - %d\n", MAX_FDS);
}

int main(int argc, char* argv[])
{
   int nf;
   int cancelcount;
   long rc = 0;
   int chunksize = CHUNKSIZE;
   unsigned char buffer[MAX_FDS * CHUNKSIZE];
   int fdlist[MAX_FDS];
   struct iocb iocb[MAX_FDS];
   struct iocb* iocblist[MAX_FDS];
   struct io_event eventlist[MAX_FDS];
   aio_context_t iocontext;
   char filename[128];

   progname = argv[0];

   if (argc != 2) {
      usage();
      return 1;
   }
   nf = atoi(argv[1]);
   if (nf <= 0 || nf > MAX_FDS) {
      usage();
      return 1;
   }
   fprintf(stdout, "Using %d files for this test run\n", nf);

   // create data for test file contents and open test files
   // and setup async io control blocks.
   for (int i = 0; i < nf; i++) {
      // fill buffer
      memset(&buffer[i * chunksize], i, chunksize);

      // open files, pid may not be unique in a container
      snprintf(filename, sizeof(filename), TEST_FILENAME, getpid(), i);
      fdlist[i] = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0777);
      if (fdlist[i] < 0) {
         fprintf(stderr, "Can't create %s, %s\n", filename, strerror(errno));
         goto done;
      }

      // fill in iocb
      iocb[i].aio_data = i;
      iocb[i].aio_rw_flags = 0;
      iocb[i].aio_lio_opcode = IOCB_CMD_PWRITE;
      iocb[i].aio_reqprio = 0;
      iocb[i].aio_fildes = fdlist[i];
      iocb[i].aio_buf = (__u64)&buffer[i * chunksize];
      iocb[i].aio_nbytes = chunksize;
      iocb[i].aio_offset = i * chunksize;
      iocb[i].aio_flags = 0;
      iocb[i].aio_resfd = -1;
      iocb[i].aio_reserved2 = 0;
      iocblist[i] = &iocb[i];
   }

   // create io context.
   rc = syscall(SYS_io_setup, 2 * nf, &iocontext);
   if (rc != 0) {
      fprintf(stderr, "SYS_io_setup failed, %s\n", strerror(errno));
      goto done;
   }
   fprintf(stdout, "IO Context 0x%lx created\n", iocontext);

   // start the i/o
   rc = syscall(SYS_io_submit, iocontext, nf, &iocblist);
   if (rc < 0) {
      fprintf(stderr, "SYS_io_submit for writes failed, %s\n", strerror(errno));
      goto done;
   }
   fprintf(stdout, "io_submit started %ld write io's\n", rc);

   // wait for io to complete
   rc = syscall(SYS_io_getevents, iocontext, nf, nf, eventlist, NULL);
   if (rc < 0) {
      fprintf(stderr, "SYS_io_getevents failed, %s\n", strerror(errno));
      goto done;
   }
   fprintf(stdout, "io_getevents returned %ld write events\n", rc);

   // Clear our buffer before reading into it.
   memset(buffer, 0xff, sizeof(buffer));

   // setup iocb to read data back into buffer
   // I think we only need to set aio_lio_opcode here.
   for (int i = 0; i < nf; i++) {
      iocb[i].aio_fildes = fdlist[i];
      iocb[i].aio_buf = (__u64)&buffer[i * chunksize];
      iocb[i].aio_lio_opcode = IOCB_CMD_PREAD;
      iocb[i].aio_nbytes = chunksize;
      iocb[i].aio_offset = i * chunksize;
      iocb[i].aio_reserved2 = 0;
   }

   // start i/o to read data
   rc = syscall(SYS_io_submit, iocontext, nf, iocblist);
   if (rc < 0) {
      fprintf(stderr, "io_submit for reads failed, %s\n", strerror(errno));
      goto done;
   }
   fprintf(stdout, "io_submit started %ld reads\n", rc);
   cancelcount = 0;

   // Find out if any reads are still active.
   struct timespec ts = {0, 0};
   rc = syscall(SYS_io_getevents, iocontext, 0, nf, &eventlist, &ts);
   if (rc < 0) {
      fprintf(stderr, "io_getevent looking for active reads failed, %s\n", strerror(errno));
      goto done;
   }
   fprintf(stdout, "%ld of %d reads have completed\n", rc, nf);
   for (int i = 0; i < nf; i++) {
      fprintf(stderr,
              "io_event@%p: data 0x%llx, obj 0x%llx, res %lld, res2 %lld\n",
              &eventlist[i],
              eventlist[i].data,
              eventlist[i].obj,
              eventlist[i].res,
              eventlist[i].res2);
   }
   // If some reads are pending, try canceling one of them
   if (rc < nf) {
      // Find a still running read
      char doneread[MAX_FDS] = {0};
      for (int i = 0; i < rc; i++) {
         doneread[eventlist[i].obj] = 1;
      }
      int found = -1;
      for (int i = 0; i < nf; i++) {
         if (doneread[i] == 0) {
            found = i;
            break;
         }
      }
      fprintf(stdout, "read for iocb[%d] is still active, trying to cancel\n", found);
      memset(&eventlist[found], 0, sizeof(struct io_event));
      rc = syscall(SYS_io_cancel, iocontext, iocblist[found], &eventlist[found]);
      if (rc < 0) {
         fprintf(stderr,
                 "io_cancel for read iocb %d, with context id 0x%lx failed, %s\n",
                 found,
                 iocontext,
                 strerror(errno));
         if (errno != EINVAL) {
            goto done;
         }
         // io_cancel() seems to fail with EINVAL even when the iocontext is valid.
         // I assume this means the io was not canceled.
         fprintf(stdout, "The read seems to have completed before we could cancel it\n");
      } else {
         fprintf(stdout, "io_cancel iocb[%d] success\n", found);
         cancelcount++;
      }

      // wait for any remaining io to complete
      rc = syscall(SYS_io_getevents, iocontext, 0, nf, &eventlist, NULL);
      if (rc < 0) {
         fprintf(stderr, "io_getevent for reads failed, %s\n", strerror(errno));
         goto done;
      }
      fprintf(stdout, "%ld read io's completed\n", rc);
      for (int i = 0; i < rc; i++) {
         fprintf(stdout,
                 "io_event[%d]: data 0x%llx, obj 0x%llx, res %lld, res2 %lld\n",
                 i,
                 eventlist[i].data,
                 eventlist[i].obj,
                 eventlist[i].res,
                 eventlist[i].res2);
      }
   } else {
      fprintf(stdout, "No outstanding reads, will not test io_cancel()\n");
   }

   // verify buffer contents
   // Since we may cancel the last io operation, we don't know if the io completed
   // or not.  So, we don't always verify its buffer contents.
   fprintf(stdout, "Verify %d buffer contents after read\n", nf - cancelcount);
   for (int i = 0; i < nf - cancelcount; i++) {
      for (int j = 0; j < chunksize; j++) {
         if (buffer[i * chunksize + j] != (unsigned char)i) {
            fprintf(stderr,
                    "fd %d, offset %d, should contain %d but contains %u\n",
                    i,
                    j,
                    i,
                    buffer[i * chunksize + j]);
         }
      }
   }

   // destroy io context
   rc = syscall(SYS_io_destroy, iocontext);
   if (rc < 0) {
      fprintf(stderr, "io_destroy failed, %s\n", strerror(errno));
      goto done;
   }
   fprintf(stdout, "IO Context 0x%lx destroyed\n", iocontext);

   fprintf(stdout, "Test completed with no errors\n");

done:;
   // close and delete test files
   for (int i = 0; i < nf; i++) {
      if (fdlist[i] < 0) {
         // If we didn't even finish opening files, stop where we stopped.
         break;
      }
      close(fdlist[i]);
      snprintf(filename, sizeof(filename), TEST_FILENAME, getpid(), i);
      unlink(filename);
   }

   return rc;
}
