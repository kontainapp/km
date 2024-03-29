/*
 * Copyright 2021 Kontain Inc
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

#include <ctype.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <linux/netlink.h>

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
   int save_errno = errno;

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
   errno = save_errno;
}

/*
 * A small routine to format an arbitrary part of memory into the km trace.
 * We produce a trace like this:
 * OOOOOO: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX  *AAAAAAAAAAAAAAAA*
 * Where OOOOOO is the offset from start of the buffer, XX is hex for a byte,
 * A is ascii for that byte
 * We handle short lengths a lengths that are not a multiple of 16.
 * This function does not decide if the trace should happen, that is the
 * caller's responsibility.
 */
void km_trace_mem(const char* function, int linenumber, const void* p, size_t l)
{
   char linebuffer[128];
   if (l == 0) {
      return;
   }
   for (unsigned int offset = 0; offset < l; offset += 16) {
      int bytecount = l - offset;
      if (bytecount > 16) {
         bytecount = 16;
      }
      char* cp = linebuffer;
      cp += snprintf(linebuffer, sizeof(linebuffer), "%06x: ", offset);
      for (int i = 0; i < 16; i++) {
         if (i < bytecount) {
            cp += sprintf(cp, "%02x ", ((unsigned char*)p)[offset + i]);
         } else {
            cp += sprintf(cp, "%s", "   ");
         }
      }
      // Now do the ascii
      *cp++ = '*';
      for (int i = 0; i < 16; i++) {
         if (i >= bytecount) {
            *cp = ' ';
         } else if (isprint(((unsigned char*)p)[offset + i])) {
            *cp = ((unsigned char*)p)[offset + i];
         } else {
            *cp = '.';
         }
         cp++;
      }
      *cp++ = '*';
      *cp = 0;
      __km_trace(0, function, linenumber, "%s", linebuffer);
   }
}

void km_trace_sockaddr(const char* function, int linenumber, const char* tag, const struct sockaddr* sap)
{
   unsigned short port;
   char addrbuffer[128];
   char linebuffer[256];
   if (sap == NULL) {
      return;
   }
   switch (sap->sa_family) {
      case AF_UNSPEC:
         snprintf(linebuffer,
                  sizeof(linebuffer),
                  "%s unspec: sa_data %02x %02x %02x ...",
                  tag,
                  sap->sa_data[0],
                  sap->sa_data[1],
                  sap->sa_data[2]);
         break;
      case AF_INET:
         inet_ntop(sap->sa_family, &((struct sockaddr_in*)sap)->sin_addr, addrbuffer, sizeof(addrbuffer));
         port = ntohs(((struct sockaddr_in*)sap)->sin_port);
         snprintf(linebuffer, sizeof(linebuffer), "%s inet4: port %u, addr %s", tag, port, addrbuffer);
         break;
      case AF_INET6:
         inet_ntop(sap->sa_family, &((struct sockaddr_in6*)sap)->sin6_addr, addrbuffer, sizeof(addrbuffer));
         port = ntohs(((struct sockaddr_in6*)sap)->sin6_port);
         snprintf(linebuffer, sizeof(linebuffer), "%s inet6: port %u, addr %s", tag, port, addrbuffer);
         break;
      case AF_UNIX:
         snprintf(linebuffer, sizeof(linebuffer), "%s unix: %s", tag, ((struct sockaddr_un*)sap)->sun_path);
         break;
      case AF_NETLINK:
         snprintf(linebuffer,
                  sizeof(linebuffer),
                  "%s netlink: nl_pid %u, nl_groups %u",
                  tag,
                  ((struct sockaddr_nl*)sap)->nl_pid,
                  ((struct sockaddr_nl*)sap)->nl_groups);
         break;
      default:
         snprintf(linebuffer,
                  sizeof(linebuffer),
                  "%s unhandled address family: %d, fixme!",
                  tag,
                  sap->sa_family);
         break;
   }
   __km_trace(0, function, linenumber, "%s", linebuffer);
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

void km_trace_fini(void)
{
   if (km_log_file != NULL) {
      fclose(km_log_file);
   }
   regfree(&km_info_trace.tags);
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
 * inherited trace fd and use trace categories from the KM_VERBOSE envrionment variable.
 */
void km_trace_setup(int argc, char* argv[])
{
   char* trace_regex = NULL;
   char* kmlogto = NULL;
   static const int regex_flags = (REG_ICASE | REG_NOSUB | REG_EXTENDED);
   int invoked_by_exec = (getenv("KM_EXEC_VERS") != NULL);

   /*
    * If we are running a snapshot and rehydrating after a snapshot
    * shrink, then KM_LOGGING should already be open.  We just need to
    * have the km logging code use the open logging fd.
    * If KM_SNAP_LISTEN_TIMEOUT has a non-zero value then we are going to
    * be running a snapshot.  If fstat(KM_LOGGING) succeeds then we know
    * we were exec'ed to by another instance of km.  The only concern is
    * if the exec was because of a payload shrink or because one non-snapshot
    * payload exec'ed into a snapshot of a payload.
    */
   struct stat statb;
   char* slt = getenv(KM_SNAP_LISTEN_TIMEOUT);
   if (slt != NULL && atol(slt) != 0 && fstat(KM_LOGGING, &statb) == 0) {
      km_redirect_msgs_after_exec();
      return;
   }

   if (invoked_by_exec == 0) {
      /*
       * We were invoked from a a shell command line.
       * Find the trace related flags from the command line and setup to operate that way.
       */
      int opt;
      int longopt_index;
      optind = 0;
      while ((opt = getopt_long(argc, argv, km_cmd_short_options, km_cmd_long_options, &longopt_index)) !=
             -1) {
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
      // No trace settings from the command line or command line is ignored, see if the environment
      // has anything to say.
      trace_regex = getenv(KM_VERBOSE);
   }
   if (kmlogto == NULL) {
      // See if the environment tells us where to log if not specified on the command line.
      kmlogto = getenv(KM_LOGTO);
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

/*
 * Produce a stack trace to the km trace destination.
 */
#define MAX_STACK_DEPTH 128
void km_pathetic_stacktrace(void)
{
   int nretaddrs;
   void* return_addresses[MAX_STACK_DEPTH];
   char** symbolic_ra;

   nretaddrs = backtrace(return_addresses, MAX_STACK_DEPTH);
   symbolic_ra = backtrace_symbols(return_addresses, nretaddrs);
   if (symbolic_ra == NULL) {
      km_warn("backtrace_symbols() returned nothing, printing %d raw addresses", nretaddrs);
      for (int j = 0; j < nretaddrs; j++) {
         km_warnx("addr %d: %p", j, return_addresses[j]);
      }
      return;
   }

   for (int j = 0; j < nretaddrs; j++) {
      km_warnx("%s", symbolic_ra[j]);
   }

   free(symbolic_ra);
}
