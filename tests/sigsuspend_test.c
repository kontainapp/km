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
 * A small sigsuspend() test.
 * Block reception of SIGUSR1.
 * Sleeps waiting for SIGUSR2 with sigsuspend().
 * Test by sending SIGUSR1 and then SIGUSR2.
 * You should see the SIGUSR2 message on stdout first and then the SIGUSR1 message.
 */
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/param.h>

void sighandler_usr1(int signo)
{
   char* message = "SIGUSR1 signal handler entered\n";
   ssize_t bytes;

   bytes = write(1, message, strlen(message));
   assert(bytes == strlen(message));
}

void sighandler_usr2(int signo, siginfo_t* sip, void* ucontext)
{
   char* message = "SIGUSR2 signal handler entered\n";
   ssize_t bytes;

   bytes = write(1, message, strlen(message));
   assert(bytes == strlen(message));
}

int main(int argc, char* argv[])
{
   sigset_t blocked;
   sigset_t blockusr2;
   struct sigaction sigaction_usr1;
   struct sigaction sigaction_usr2;
   int rc;
   char* flagfile = NULL;
   struct stat statb;

   if (argc == 2) {
      flagfile = argv[1];
      if (stat(flagfile, &statb) == 0) {
         fprintf(stderr, "%s exists, it shouldn't, remove before running this test\n", flagfile);
         return 1;
      }
   }

   sigaction_usr1.sa_handler = sighandler_usr1;
   sigemptyset(&sigaction_usr1.sa_mask);
   sigaction_usr1.sa_flags = 0;
   sigaction_usr1.sa_restorer = NULL;
   rc = sigaction(SIGUSR1, &sigaction_usr1, NULL);
   assert(rc == 0);

   sigaction_usr2.sa_sigaction = sighandler_usr2;
   sigemptyset(&sigaction_usr2.sa_mask);
   sigaction_usr2.sa_flags = SA_SIGINFO;
   sigaction_usr2.sa_restorer = NULL;
   rc = sigaction(SIGUSR2, &sigaction_usr2, NULL);
   assert(rc == 0);

   sigemptyset(&blockusr2);
   sigaddset(&blockusr2, SIGUSR2);
   rc = sigprocmask(SIG_SETMASK, &blockusr2, NULL);
   assert(rc == 0);

   /*
    * This flag file is used to tell the test driver when it is almost
    * safe to start sending signals to this program.  If you are running
    * this test by hand you don't need to worry about this.  When being
    * run under azure things can be so slow that the test script is ready
    * before the this program is even spun off into the background.
    */
   if (flagfile != NULL) {
      int ff_fd = open(flagfile, O_CREAT, 0777);
      if (ff_fd < 0) {
         fprintf(stderr, "Couldn't create %s, %s\n", flagfile, strerror(errno));
         return 1;
      }
      close(ff_fd);
   }

   sigemptyset(&blocked);
   sigaddset(&blocked, SIGUSR1);
   rc = sigsuspend(&blocked);
   printf("sigsuspend returned %d, errno %d\n", rc, errno);
   assert(rc < 0 && errno == EINTR);

   if (flagfile != NULL) {
      unlink(flagfile);
   }

   return 0;
}
