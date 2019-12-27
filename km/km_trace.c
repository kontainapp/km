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

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>
#include <string.h>
#include <errno.h>

/*
 * Verbose trace function.
 * We get a timestamp, function name, line number, and thread id in the trace line
 * in addition to whatever the caller is trying to trace.
 * In addition we build all of this into a single buffer which we feed to fputs()
 * in the hope that trace lines are not broken when multiple threads are tracing
 * concurrently.
 */
void km_trace(int want_strerror, const char* function, int linenumber, const char* fmt, ...)
{
   char traceline[512];
   size_t tlen;
   struct timespec ts;
   struct tm tm;
   char* p;
   pid_t tid;
   int errnum = errno;
   va_list ap;

   va_start(ap, fmt);

   tid = syscall(SYS_gettid);
   clock_gettime(CLOCK_REALTIME, &ts);
   gmtime_r(&ts.tv_sec, &tm);            // UTC!
   snprintf(traceline, sizeof(traceline), "%02d/%02d/%04d %02d:%02d:%02d.%09ld %-16.16s %5d %5d ",
             tm.tm_mon + 1,
             tm.tm_mday,
             tm.tm_year + 1900,
             tm.tm_hour,
             tm.tm_min,
             tm.tm_sec,
             ts.tv_nsec,
             function,
             linenumber,
             tid);
   tlen = strlen(traceline);
   p = &traceline[tlen];
   vsnprintf(p, sizeof(traceline) - tlen, fmt, ap);

   if (want_strerror != 0) {
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
   traceline[tlen+1] = 0;

   va_end(ap);

   fputs(traceline, stderr);

   /*
    * We probably want fflush(stderr) so we get the last bit of output if we abort/fault
    */
   fflush(stderr);
}
