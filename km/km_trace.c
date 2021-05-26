/*
 * Copyright © 2019-2021 Kontain Inc. All rights reserved.
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

/*
 * This function sets up km tracing and is intended to be called very early in km
 * startup.  The goal is to have functional tracing when km is entered via an execve()
 * call from a km payload but we also handle trace setup when km is invoked from a shell
 * command line.
 * When invoked from the shell we locate the tracing related arguments in argv[] and
 * set things up as desired by the invoker.
 * The command line args take precedence over the KM_VERBOSE variable's value if both
 * are present when invoked from the shell.
 * When invoked from a km payload we ignore km command line trace settings and use the
 * inherited trace fd and use trace categoeries from the KM_VERBOSE envrionment variable.
 */
void km_trace_setup(int argc, char* argv[], char* payload_name)
{
   char* trace_regex = NULL;
   char* kmlogto = NULL;
   static const int regex_flags = (REG_ICASE | REG_NOSUB | REG_EXTENDED);
   int invoked_by_exec = (getenv("KM_EXEC_VERS") != NULL);

   if (invoked_by_exec == 0 && payload_name == NULL) {
      /*
       * We were invoked from a a shell command line.
       * Find the trace related flags from the command line and setup to operate that way.
       */
      int opt;
      int longopt_index;
      optind = 0;
      while ((opt = getopt_long(argc, argv, km_cmd_short_options, km_cmd_long_options, &longopt_index)) != -1) {
         switch (opt) {
            case 'V':
               trace_regex = (optarg != NULL) ? optarg : "";
               break;
            case 'k':
               kmlogto = optarg;
               break;
            case '?':
               // Invalid option, do nothing, let km_parse_args() find the problem and report.
               return;
            default:
               // Ignore the valid options, they are handled in km_parse_args();
               break;
         }
      }
   }

   if (trace_regex == NULL) {
      // No trace settings from the command line or command line is ignored, see if the environment has anything to say.
      trace_regex = getenv(KM_VERBOSE);
   }
   if (trace_regex != NULL) {
      if (*trace_regex == 0) {
         km_info_trace.level = KM_TRACE_INFO;
      } else {
         km_info_trace.level = KM_TRACE_TAG;
         if (regcomp(&km_info_trace.tags, trace_regex, regex_flags) != 0) {
            km_warnx("Failed to compile trace regular expression '%s'", trace_regex);
         }
      }
   }

   // We know what to trace, setup where the traces go.
   if (invoked_by_exec == 0) {
      km_redirect_msgs(kmlogto);
   } else {
      /*
       * km was started by execve() from a km payload.  So, the logging destination fd is inherited
       * from the exec'ing km.  What is logged is controled only by the setting of the KM_VERBOSE
       * environment variable.
       */
      km_redirect_msgs_after_exec();
   }
}
