/*
 * Copyright © 2019-2020 Kontain Inc. All rights reserved.
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
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "km.h"
#include "km_exec.h"

uint8_t km_trace_pid = 0;
uint8_t km_trace_noniteractive = 0;

FILE* km_log_file;
static char km_log_file_name[128];

// To avoid having /tmp cluttered with empty km log files, we open the log on demand.
static inline void km_trace_open_log_on_demand(void)
{
   extern const int KM_LOGGING;
   if (km_log_file == NULL && km_log_file_name[0] != 0) {
      int fd;
      int fd1 = open(km_log_file_name, O_CREAT | O_WRONLY, 0644);
      if (fd1 >= 0) {
         fd = dup2(fd1, KM_LOGGING);
         close(fd1);
         km_log_file = fdopen(fd, "w");
         if (km_log_file != NULL) {
            setlinebuf(km_log_file);
         } else {
            close(KM_LOGGING);
         }
      }
      // Only try "open on demand" once.
      km_log_file_name[0] = 0;
   }
}

/*
 * Verbose trace function.
 * We get a timestamp, function name, line number, and thread id in the trace line in addition to
 * whatever the caller is trying to trace. In addition we build all of this into a single buffer
 * which we feed to fputs() in the hope that trace lines are not broken when multiple threads are
 * tracing concurrently.
 */
void __km_trace(int errnum, const char* function, int linenumber, const char* fmt, ...)
{
   char traceline[512];
   char threadname[16];
   size_t tlen;
   struct timespec ts;
   struct tm tm;
   char* p;
   va_list ap;

   km_trace_open_log_on_demand();

   va_start(ap, fmt);

   km_getname_np(pthread_self(), threadname, sizeof(threadname));
   clock_gettime(CLOCK_REALTIME, &ts);
   gmtime_r(&ts.tv_sec, &tm);   // UTC!
   if (km_trace_pid != 0) {
      snprintf(traceline,
               sizeof(traceline),
               "%02d:%02d:%02d.%06ld %-20.20s %-4d %4d.%-7.7s ",
               tm.tm_hour,
               tm.tm_min,
               tm.tm_sec,
               ts.tv_nsec / 1000,   // convert to microseconds
               function,
               linenumber,
               machine.pid,
               threadname);
   } else {
      snprintf(traceline,
               sizeof(traceline),
               "%02d:%02d:%02d.%06ld %-20.20s %-4d %-7.7s ",
               tm.tm_hour,
               tm.tm_min,
               tm.tm_sec,
               ts.tv_nsec / 1000,   // convert to microseconds
               function,
               linenumber,
               threadname);
   }
   tlen = strlen(traceline);
   p = &traceline[tlen];
   vsnprintf(p, sizeof(traceline) - tlen, fmt, ap);

   if (errnum != 0) {
      tlen = strlen(traceline);
      if ((sizeof(traceline) - tlen) > 5) {   // be safe
         strcpy(traceline + tlen, ": ");
         tlen = strlen(traceline);
         char* s = strerror_r(errnum, &traceline[tlen], sizeof(traceline) - tlen);
         if (s != traceline + tlen) {   // see 'man 3 strerror_r'
            strncpy(traceline + tlen, s, sizeof(traceline) - tlen);
         }
      }
   }

   tlen = strlen(traceline);
   if (tlen + 1 > sizeof(traceline)) {
      tlen = sizeof(traceline) - 2;
   }
   traceline[tlen] = '\n';
   traceline[tlen + 1] = 0;

   va_end(ap);

   if (km_log_file != NULL) {
      fputs(traceline, km_log_file);
   } else if (stderr != NULL) {
      if (km_trace_noniteractive == 1 || km_trace_pid != 0 || km_called_via_exec() == 1) {
         fputs(traceline, stderr);
      } else {
         fputs(p, stderr);
      }
   }
}

void km_trace_include_pid(uint8_t trace_pid)
{
   km_trace_pid = trace_pid;
}

void km_trace_set_noninteractive(void)
{
   km_trace_noniteractive = 1;
}

uint8_t km_trace_include_pid_value(void)
{
   return km_trace_pid;
}

void km_trace_set_log_file_name(char* kmlog_file_name)
{
   snprintf(km_log_file_name, sizeof(km_log_file_name), "%s", kmlog_file_name);
   if (strlen(kmlog_file_name) >= sizeof(km_log_file_name)) {
      km_warnx("Truncating log file name %s to %s", kmlog_file_name, km_log_file_name);
   }
}
