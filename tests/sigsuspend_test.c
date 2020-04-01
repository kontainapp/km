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
 */

/*
 * A small sigsuspend() test.
 * Block reception of SIGUSR1.
 * Sleeps waiting for SIGUSR2 with sigsuspend().
 * Test by sending SIGUSR1 and then SIGUSR2.
 * You should see the SIGUSR2 message on stdout first and then the
 * SIGUSR1 message.
 */
#include <stdio.h>
#include <signal.h>
#define SIGSET_INITIALIZER { 0 }
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

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

int main(int argc, char *argvp[])
{
   sigset_t blocked;
   sigset_t blockusr2;
   struct sigaction sigaction_usr1;
   struct sigaction sigaction_usr2;
   int rc;

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

   sigemptyset(&blocked);
   sigaddset(&blocked, SIGUSR1);
   rc = sigsuspend(&blocked);
   printf("sigsuspend returned %d, errno %d\n", rc, errno);
   assert(rc < 0 && errno == EINTR);

   return 0;
}
