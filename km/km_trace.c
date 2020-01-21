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
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "km.h"

/*
 * Verbose trace function.
 * We get a timestamp, function name, line number, and thread id in the trace line
 * in addition to whatever the caller is trying to trace.
 * In addition we build all of this into a single buffer which we feed to fputs()
 * in the hope that trace lines are not broken when multiple threads are tracing
 * concurrently.
 */
void km_trace(int errnum, const char* function, int linenumber, const char* fmt, ...)
{
   char traceline[512];
   char threadname[16];
   size_t tlen;
   struct timespec ts;
   struct tm tm;
   char* p;
   va_list ap;

   va_start(ap, fmt);

   km_getname_np(pthread_self(), threadname, sizeof(threadname));
   clock_gettime(CLOCK_REALTIME, &ts);
   gmtime_r(&ts.tv_sec, &tm);   // UTC!
   snprintf(traceline,
            sizeof(traceline),
            "%02d:%02d:%02d.%06ld %-20.20s %5d %-6.6s ",
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            ts.tv_nsec / 1000,   // convert to microseconds
            function,
            linenumber,
            threadname);
   tlen = strlen(traceline);
   p = &traceline[tlen];
   vsnprintf(p, sizeof(traceline) - tlen, fmt, ap);

   if (errnum != 0) {
      tlen = strlen(traceline);
      if ((sizeof(traceline) - tlen) > 2) {
         strerror_r(errnum, &traceline[tlen], sizeof(traceline) - tlen);
      }
   }

   tlen = strlen(traceline);
   if (tlen + 1 > sizeof(traceline)) {
      tlen = sizeof(traceline) - 2;
   }
   traceline[tlen] = '\n';
   traceline[tlen + 1] = 0;

   va_end(ap);

   fputs(traceline, stderr);

   /*
    * We probably want fflush(stderr) so we get the last bit of output if we abort/fault
    */
   fflush(stderr);
}
