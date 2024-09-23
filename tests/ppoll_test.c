#include <errno.h>
#include <poll.h>   // _GNU_SOURCE must be defined to get ppoll()
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum pipedir { readside = 0, writeside = 1 };

int main(int argc, char* argv[])
{
   int pipefd[2];

   if (pipe(pipefd) < 0) {
      fprintf(stderr, "pipe() failed, %s\n", strerror(errno));
      return 1;
   }

   struct timespec ts = {0, 1000};   // 1 microsecond
   struct pollfd fdlist[] = {{pipefd[readside], POLLIN, 0}};
   int rc;

   // ppoll() when the pipe is empty, should timeout
   if ((rc = ppoll(fdlist, 1, &ts, NULL)) != 0) {
      fprintf(stderr, "ppoll() with empty pipe should timeout and return 0, got %d\n", rc);
      return 1;
   }

   // put something in the pipe
   char buf[32];
   memset(buf, 22, sizeof(buf));
   ssize_t bw = write(pipefd[writeside], buf, sizeof(buf));
   if (bw != sizeof(buf)) {
      fprintf(stderr, "write to pipe should have returned %lu, actually returned %ld\n", sizeof(buf), bw);
      return 1;
   }

   // ppoll() on pipe with something in it should return 1 for this test.
   if ((rc = ppoll(fdlist, 1, &ts, NULL)) != 1) {
      fprintf(stderr, "ppoll() on pipe with data should return 1, got %d\n", rc);
      return 1;
   }

   fprintf(stdout, "simple test of ppoll() passed\n");

   close(pipefd[writeside]);
   close(pipefd[readside]);

   return 0;
}
