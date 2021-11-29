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
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "km.h"
#include "km_coredump.h"
#include "km_elf.h"
#include "km_exec.h"
#include "km_filesys.h"
#include "km_fork.h"
#include "km_gdb.h"
#include "km_management.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_snapshot.h"

km_info_trace_t km_info_trace;
char* km_payload_name;

extern int vcpu_dump;

/*
 * Logging and printing.
 *
 * We separate km file descriptors from payload file descriptors. The former are in the separate
 * range, upper MAX_KM_FILES, guest fds have the same numerical value in the guest and in km.
 *
 * Standard err()/warn() functions are not used any more (other than in parsing argvs). The
 * stdout/stderr in km are closed after argvs parsing is done, so printf() and such will not work.
 * Use km_warnx() instead.
 *
 * The functions we should use going forward are:
 * - km_info()/km_infox() - goes by -V option or KM_TRACE environment variable, printing trace
 *                          messages per facility
 * - km_trace()/km_tracex() - Generic trace messages enabled by any -V
 * - km_warn()/km_warnx() - unconditionally prints message and continues (use for printf and such)
 * - km_err()/km_errx() - unconditionally prints message and exits
 */

static inline void usage()
{
   if (km_called_via_exec() == 1) {
      exit(1);
   }
   // clang-format off
   errx(1,
"Kontain Monitor - runs 'payload-file [payload args]' in Kontain VM\n"
"Usage: km [options] payload-file[.km] [payload_args ... ]\n"
"\n"
"Options:\n"
"\t--verbose[=regexp] (-V[regexp])     - Verbose print where internal info tag matches 'regexp'\n"
"\t--gdb-server-port[=port] (-g[port]) - Enable gbd server listening on <port> (default 2159)\n"
"\t--gdb-listen                        - gdb server listens for client while payload runs\n"
"\t--gdb-dynlink                       - gdb server waits for client attach before dyn link runs\n"
"\t--version (-v)                      - Print version info and exit\n"
"\t--log-to=file_name (-l file_name)   - Stream guest stdout and stderr to file_name\n"
"\t--km-log-to=file_name (-k file_name)- Stream km log to file_name\n"
"\t--putenv key=value                  - Add environment 'key' to payload (cancels host env)\n"
"\t--wait-for-signal                   - Wait for SIGUSR1 before running payload\n"
"\t--dump-shutdown                     - Produce register dump on VCPU error\n"
"\t--core-on-err                       - generate KM core dump when exiting on err, including guest core dump\n"
"\t--overcommit-memory                 - Allow huge address allocations for payloads.\n"
"\t                                      See 'sysctl vm.overcommit_memory'\n"
"\t--hcall-stats (-S)                  - Collect and print hypercall stats\n"
"\t--coredump=file_name                - File name for coredump\n"
"\t--snapshot=file_name                - File name for snapshot\n"
"\n"
"\tOverride auto detection:\n"
"\t--membus-width=size (-Psize)        - Set guest physical memory bus size in bits, i.e. 32 means 4GiB, 33 8GiB, 34 16GiB, etc.\n"
"\t--enable-1g-pages                   - Force enable 1G pages support (default). Assumes hardware support\n"
"\t--disable-1g-pages                  - Force disable 1G pages support\n"
"\t--virt-device=<file-name>  (-Ffile) - Use provided file-name for virtualization device\n"
"\t--input-data=<file-name>            - File with data for HC_snapshot_getdata\n"
"\t--output-data=<file-name>           - File with data from HC_snapshot_putdata\n"
"\t--mgtpipe <path>                    - Name for management pipe.\n");
   // clang-format on
}

// Version info. SCM_* is supposed to be set by the build
static const int ver_major = 0;
static const int ver_minor = 1;
static char* ver_type = "-beta";

// these 2 macros allow to -Dsome_var=str_value without double quotes around str_value
#define __STR(_x) #_x
#define _STR_VALUE(_x) __STR(_x)

#ifndef SRC_BRANCH   // branch name
#define SRC_BRANCH ?
#endif
#ifndef SRC_VERSION   // SHA
#define SRC_VERSION ?
#endif
#ifndef BUILD_TIME
#define BUILD_TIME ?
#endif

static inline void show_version(void)
{
   km_errx(0,
           "Kontain Monitor version %d.%d%s\nBranch: %s sha: %s build_time: %s",
           ver_major,
           ver_minor,
           ver_type,
           _STR_VALUE(SRC_BRANCH),
           _STR_VALUE(SRC_VERSION),
           _STR_VALUE(BUILD_TIME));
}

// Option names we use elsewhere.
#define GDB_LISTEN "gdb-listen"
#define GDB_DYNLINK "gdb-dynlink"

km_machine_init_params_t km_machine_init_params = {
    .force_pdpe1g = KM_FLAG_FORCE_ENABLE,
    .overcommit_memory = KM_FLAG_FORCE_DISABLE,
    .vdev_name = NULL,   // redundant, but let's be paranoid
};
static int wait_for_signal = 0;
int debug_dump_on_err = 0;   // if 1, will abort() instead of err()
static char* mgtpipe = NULL;
static int log_to_fd = -1;
extern int set_cpu_vendor_id;

struct option km_cmd_long_options[] = {
    {"wait-for-signal", no_argument, &wait_for_signal, 1},
    {"dump-shutdown", no_argument, 0, 'D'},
    {"enable-1g-pages", no_argument, &(km_machine_init_params.force_pdpe1g), KM_FLAG_FORCE_ENABLE},
    {"disable-1g-pages", no_argument, &(km_machine_init_params.force_pdpe1g), KM_FLAG_FORCE_DISABLE},
    {"overcommit-memory", no_argument, &(km_machine_init_params.overcommit_memory), KM_FLAG_FORCE_ENABLE},
    {"coredump", required_argument, 0, 'C'},
    {"membus-width", required_argument, 0, 'P'},
    {"vendorid", no_argument, &set_cpu_vendor_id, KM_FLAG_FORCE_ENABLE},
    {"log-to", required_argument, 0, 'l'},
    {"km-log-to", required_argument, 0, 'k'},
    {"putenv", required_argument, 0, 'e'},
    {"gdb-server-port", optional_argument, 0, 'g'},
    {GDB_LISTEN, no_argument, NULL, 0},
    {GDB_DYNLINK, no_argument, NULL, 0},
    {"verbose", optional_argument, 0, 'V'},
    {"core-on-err", no_argument, &debug_dump_on_err, 1},
    {"version", no_argument, 0, 'v'},
    {"hcall-stats", no_argument, 0, 'S'},
    {"virt-device", required_argument, 0, 'F'},
    {"snapshot", required_argument, 0, 's'},
    {"input-data", required_argument, 0, 'I'},
    {"output-data", required_argument, 0, 'O'},
    {"mgtpipe", required_argument, 0, 'm'},

    {0, 0, 0, 0},
};

const_string_t km_cmd_short_options = "+g::e:AV::F:P:vC:Sk:";

static const_string_t SHEBANG = "#!";
static const size_t SHEBANG_LEN = 2;              // strlen(SHEBANG)
static const size_t SHEBANG_MAX_LINE_LEN = 512;   // note: grabbed on stack

char* km_get_self_name()
{
   // full path to KM (pointed to by /proc/self/exe).
   static char* km_bin_name;   // TODO - better place for it ?

   if (km_bin_name == NULL) {
      char buf[PATH_MAX + 1];
      int bytes;

      if ((bytes = readlink(PROC_SELF_EXE, buf, PATH_MAX)) < 0) {
         km_warn("Failed to read %s", PROC_SELF_EXE);
         return NULL;
      }
      buf[bytes] = '\0';
      km_bin_name = strdup(buf);
   }
   return km_bin_name;
}

static int one_readlink(const char* name, char* buf, size_t buf_size)
{
   int rc;
   if ((rc = readlink(name, buf, buf_size)) < 0) {
      return -1;
   }
   if (*buf == '/') {
      return rc;
   }

   // link is relative - construct full path name
   buf[rc] = 0;
   char* name_l = strdupa(name);
   char* link = strdupa(buf);
   snprintf(buf, buf_size - 1, "%s/%s", dirname(name_l), link);
   return strlen(buf);
}

// check if name point to km ourselves. Return -1 for errors, 1 if we are km, 0 if not
static int km_check_for_km(const char* name)
{
   struct stat stb, stb_self;

   if (stat(PROC_SELF_EXE, &stb_self) != 0 || stat(name, &stb) != 0) {
      return -1;
   }
   return (stb_self.st_dev == stb.st_dev && stb_self.st_ino == stb.st_ino) ? 1 : 0;
}

// returns strdup-ed name of the final symlink (NOT the final file), NULL if there was none
char* km_traverse_payload_symlinks(const char* arg)
{
   char buf[PATH_MAX + 1];
   int bytes;
   // we need to keep prior symlink name so the upstairs can locate the payload
   char* last_symlink = NULL;
   char* name = strdup(arg);

   while ((bytes = one_readlink(name, buf, PATH_MAX)) != -1) {
      buf[bytes] = '\0';
      if (last_symlink != NULL) {
         free(last_symlink);
      }
      last_symlink = name;
      name = strdup(buf);
   }
   free(name);
   if (last_symlink != NULL) {
      snprintf(buf, PATH_MAX, "%s%s", last_symlink, PAYLOAD_SUFFIX);
      free(last_symlink);
      if ((last_symlink = realpath(buf, NULL)) == NULL) {
         km_err(1, "realpath(%s) failed", buf);
      }
   }
   return last_symlink;
}

static int is_eol(const char* ch)
{
   return (*ch == '\n' || *ch == '\r') ? 1 : 0;
}

/*
 * Check if the passed file is a shebang, and if it is get payload file name from there. Returns
 * strdup-ed payload name on success, NULL if no shebang is present
 */
char* km_parse_shebang(const char* payload_file, char** extra_arg)
{
   char line_buf[SHEBANG_MAX_LINE_LEN + 1];   // first line of shebang file
   int fd;
   int count;

   assert(payload_file != NULL);
   km_infox(KM_TRACE_EXEC, "input payload_file %s", payload_file);
   *extra_arg = NULL;

   if ((fd = open(payload_file, O_RDONLY, 0)) < 0) {
      km_trace("open(%s) failed", payload_file);
      return NULL;
   }
   count = pread(fd, line_buf, SHEBANG_MAX_LINE_LEN, 0);
   close(fd);

   if (count <= SHEBANG_LEN) {
      km_warn("Failed to read even %ld bytes from %s", SHEBANG_LEN, payload_file);
      return NULL;
   }

   line_buf[count] = 0;   // null terminate whatever we read
   if (strncmp(line_buf, SHEBANG, SHEBANG_LEN) != 0) {
      return NULL;
   }
   km_tracex("Extracting payload name from shebang file '%s'", payload_file);
   char* c;
   for (c = line_buf + SHEBANG_LEN; isblank(*c) == 0 && is_eol(c) == 0 && *c != '\0'; c++) {
   }   // find args, if any
   if (*c == '\0') {
      km_warnx("Warning: failed to find file name to execute (line too long?): %s", payload_file);
   } else if (is_eol(c) == 0) {
      *c++ = '\0';   // null terminate the payload name
      for (; isblank(*c) == 1; c++) {
      }   // skip blanks
   }
   if (is_eol(c) == 0 && *c != '\0') {
      char* arg_end;
      for (arg_end = c; *arg_end != '\0' && is_eol(arg_end) == 0; arg_end++) {
      }
      if (arg_end != c) {   // arg found
         *arg_end = '\0';
         *extra_arg = strdup(c);
         km_tracex("Found arg: '%s'", *extra_arg);
      }
   } else {
      *c++ = '\0';   // null terminate the payload name
   }
   payload_file = line_buf + SHEBANG_LEN;
   km_tracex("Payload file from shebang: '%s'", payload_file);
   if (km_do_shell != 0) {
      if (km_is_env_path(payload_file) == 1) {
         payload_file = *extra_arg;
         *extra_arg = NULL;
      }
      char* final_symlink;
      if ((final_symlink = km_traverse_payload_symlinks(payload_file)) == NULL) {
         // a likely access issue, let's return the name as is for future diagnosics
         km_tracex("Failed to resolve symlinks, returning the name as is");
         return (strdup(payload_file));
      }
      return final_symlink;
   }
   return strdup(payload_file);
}

/*
 * Change our argv strings to look like payload rather than km.
 * This makes tools like ps show payload, and helps monitoring tools to discover payloads.
 *
 * argv array points to optarg strings. Optarg strings are stored as back to back null terminated
 * strings. /proc/self/cmdline shows the content of that area, tools like ps do the same, likely
 * just reading /prco/pid/cmdline.
 * km args are organized so that km specific args go first, followed by guest args. This function
 * will shift (memmove) guest optargs to the beginning of the optargs area, memzero the tail, and
 * readjust argv pointers accordingly.
 */

static void km_mimic_payload_argv(int argc, char** argv, int pl_index)
{
   extern char* __progname;

   if (pl_index > 0 && pl_index < argc) {
      char* end = argv[argc - 1] + strlen(argv[argc - 1]) + 1;
      int size = end - argv[pl_index];
      int shift = argv[pl_index] - argv[0];
      assert(shift + size == end - argv[0]);

      memmove(argv[0], argv[pl_index], size);
      memset(argv[0] + size, 0, shift);
      for (int i = 0; i < argc - pl_index; i++) {
         argv[i] = argv[i + pl_index] - shift;
      }
      __progname = argv[0];
   }
}

// parses args, returns payload_name based on argv[], symlink, or shebang.
// also fills *argc_p, *argv_p, *envc_p and *envp_p with args/env info for payload
static char* km_parse_args(
    int argc, char* argv[], int* argc_p, char** argv_p[], int* envc_p, char** envp_p[], char* pl_name)
{
   int opt;
   uint64_t port;
   int gpbits = 0;         // Width of guest physical memory bus.
   int copyenv_used = 1;   // By default copy environment from host
   int putenv_used = 0;
   char* ep = NULL;
   int longopt_index;    // flag index in longopt array
   int pl_index = 0;     // payload_name index in argv array
   char** envp = NULL;   // NULL terminated array of env pointers
   int envc = 1;   // count of elements in envp (including NULL), see realloc below in case 'e'

   if (pl_name == NULL) {   // regular KM invocation - parse KM args
      optind = 0;           // reinit getopt
      while ((opt = getopt_long(argc, argv, km_cmd_short_options, km_cmd_long_options, &longopt_index)) !=
             -1) {
         switch (opt) {
            case 0:
               // If this option set a flag, do nothing else now.
               if (km_cmd_long_options[longopt_index].flag != 0) {
                  break;
               }
               // Put here handling of longopts which do not have related short opt
               if (strcmp(km_cmd_long_options[longopt_index].name, GDB_LISTEN) == 0) {
                  gdbstub.wait_for_attach = GDB_DONT_WAIT_FOR_ATTACH;
                  km_gdb_enable(1);
               } else if (strcmp(km_cmd_long_options[longopt_index].name, GDB_DYNLINK) == 0) {
                  gdbstub.wait_for_attach = GDB_WAIT_FOR_ATTACH_AT_DYNLINK;
                  km_gdb_enable(1);
               }
               break;
            case 'g':   // enable the gdb server and specify a port to listen on
               if (optarg != NULL) {
                  char* endp = NULL;
                  errno = 0;
                  port = strtoul(optarg, &endp, 0);
                  if (errno != 0 || (endp != NULL && *endp != 0)) {
                     km_warnx("Invalid gdb port number '%s'", optarg);
                     usage();
                  }
               } else {
                  port = GDB_DEFAULT_PORT;
               }
               km_gdb_port_set(port);
               km_gdb_enable(1);
               if (gdbstub.wait_for_attach == GDB_WAIT_FOR_ATTACH_UNSPECIFIED) {   // wait at _start
                                                                                   // by default
                  gdbstub.wait_for_attach = GDB_WAIT_FOR_ATTACH_AT_START;
               }
               break;
            case 'l':
               if ((log_to_fd = open(optarg, O_WRONLY | O_CREAT, 0644)) < 0) {
                  km_err(1, "--log-to %s", optarg);
               }

               break;
            case 'k':
               // km logging destination is setup in km_trace_setup() called earlier.  We ignore this here.
               break;
            case 'e':                    // --putenv
               if (copyenv_used > 1) {   // if --copyenv was on the command line, something is wrong
                  km_warnx("Wrong options: '--putenv' cannot be used with together with "
                           "'--copyenv'");
                  usage();
               }
               copyenv_used = 0;   // --putenv cancels 'default' --copyenv
               putenv_used++;
               envc++;
               if ((envp = realloc(envp, sizeof(char*) * envc)) == NULL) {
                  km_err(1, "Failed to alloc memory for putenv %s", optarg);
               }
               if ((envp[envc - 2] = strdup(optarg)) == NULL) {
                  km_err(1, "Failed to alloc memory for putenv %s value", optarg);
               }
               envp[envc - 1] = NULL;
               break;
            case 'C':
               km_set_coredump_path(optarg);
               break;
            case 'F':
               km_machine_init_params.vdev_name = strdup(optarg);
               break;
            case 's':
               km_set_snapshot_path(optarg);
               break;
            case 'D':
               vcpu_dump = 1;
               break;
            case 'P':
               ep = NULL;
               gpbits = strtol(optarg, &ep, 0);
               if (ep == NULL || *ep != '\0') {
                  km_warnx("Wrong memory bus size '%s'", optarg);
                  usage();
               }
               if (gpbits < 32 || gpbits >= 63) {
                  km_warnx("Guest memory bus width must be between 32 and 63 - got '%d'", gpbits);
                  usage();
               }
               km_machine_init_params.guest_physmem = 1UL << gpbits;
               if (km_machine_init_params.guest_physmem > GUEST_MAX_PHYSMEM_SUPPORTED) {
                  km_warnx("Guest physical memory must be < 0x%lx - got 0x%lx (bus width %d)",
                           GUEST_MAX_PHYSMEM_SUPPORTED,
                           km_machine_init_params.guest_physmem,
                           gpbits);
                  usage();
               }
               break;
            case 'V':
               // trace categories are setup earlier in km_trace_setup().  We do nothing here.
               break;
            case 'v':
               show_version();
               break;
            case 'S':
               km_collect_hc_stats = 1;
               break;
            case 'I':
               km_set_snapshot_input_path(optarg);
               break;
            case 'O':
               km_set_snapshot_output_path(optarg);
               break;
            case 'm':
               mgtpipe = strdup(optarg);
               break;
            case ':':
               km_warnx("Missing arg for %c", optopt);
               usage();
            default:
               usage();
         }
      }
      pl_index = optind;
   }

   if (mgtpipe == NULL) {
      char* mgt_e = getenv(KM_MGTPIPE);
      if (mgt_e != NULL) {
         mgtpipe = strdup(mgt_e);
      }
   }

   // Configure payload's env and args
   *envp_p = envp;
   *envc_p = envc;
   // TODO: fix for snapshot restore
   km_mimic_payload_argv(argc, argv, pl_index);
   *argc_p = argc - pl_index;
   *argv_p = argv;

   /*
    * If we were called by shebang/symlink, shebang was processed by the system and symlink at the
    * top of this function. However there is a case when we explicitly call km with shebang
    * argument. Note that km_parse_shebang() follows symlinks via open() so symlinks to shebang also
    * works.
    */
   if (pl_name == NULL) {
      if (pl_index == argc) {
         usage();
      }
      char* extra_arg = NULL;   // shebang arg (if any) will be placed here, strdup-ed
      if ((pl_name = km_parse_shebang(argv[0], &extra_arg)) != NULL) {
         int idx = 0;
         (*argc_p)++;   // room for shebang file name
         if (extra_arg != NULL) {
            (*argc_p)++;   // room for shebang arg
            *argv_p = calloc(*argc_p, sizeof(char*));
            (*argv_p)[idx++] = pl_name;
            (*argv_p)[idx++] = extra_arg;   // shebang has only one arg
         } else {
            *argv_p = calloc(*argc_p, sizeof(char*));
            (*argv_p)[idx++] = pl_name;
         }
         (*argv_p)[idx++] = argv[0];   // shebang file

         memcpy(*argv_p + idx, argv + 1, sizeof(char*) * (*argc_p - idx));   // payload args
      } else {
         /*
          * We didn't find .km file either through symlink or shebang. This could be legitimate .km
          * file or some garbage, and we'll know for sure when we try to load it as ELF. However,
          * since we now have symlinks to km executable lying around check we are not trying to load
          * ourselves as payload.
          */
         if (km_check_for_km(argv[0]) == 0) {
            pl_name = realpath(argv[0], NULL);
         }
      }
   }
   km_exec_init_args(*argc_p, *argv_p);
   return pl_name;
}

// clang-format off
/*
 * Decide if gdbstub should start up with all vcpu's paused or running.
 * Returns:
 *   1 - vcpu's are paused
 *   0 - vcpu's are running.
 *
 * Truth table used to decide if km should pause vcpus' to wait for gdb client connect on payload startup;
 *
 * km started as result of manually running payload
 * gdb             gdb client      wait to         vcpu's
 * enabled         connected       connect         paused
 * 0               0               0               0
 * 0               0               1               x - can't happen gdb disabled
 * 0               1               0               x - gdb client can't be connected
 * 0               1               1               x - gdb client can't be connected
 * 1               0               0               0 - listen in the background for client connection
 * 1               0               1               1 - wait for gdb client connect
 * 1               1               0               x - gdb client can't be connected
 * 1               1               1               x - gdb client can't be connected
 *
 * km started as a result of another km execing to payload
 * gdb             gdb client      wait to         vcpu's
 * enabled         connected       connect         paused
 * 0               0               0               0
 * 0               0               1               x - can't happen gdb disabled
 * 0               1               0               x - can't happen gdb disabled
 * 0               1               1               x - can't happen gdb disabled
 * 1               0               0               0 - gdb stub waits in the background for connections
 * 1               0               1               0 - gdb stub waits for connection in background
 * 1               1               0               1 - need to send exec event to gdb client
 * 1               1               1               1 - need to send exec event to gdb client
 */
// clang-format on
static inline int km_need_pause_all(void)
{
   km_infox(KM_TRACE_EXEC,
            "km_called_via_exec %d, send_exec_event %d, wait_for_attach %d, "
            "km_gdb_client_is_attached %d",
            km_called_via_exec(),
            gdbstub.send_exec_event,
            gdbstub.wait_for_attach,
            km_gdb_client_is_attached());
   return ((km_called_via_exec() == 0 && gdbstub.wait_for_attach != GDB_DONT_WAIT_FOR_ATTACH) ||
           (km_called_via_exec() != 0 && km_gdb_client_is_attached() != 0));
}

int km_do_shell = 1;

static void km_check_do_shell(void)
{
   char* cp = getenv(KM_DO_SHELL);
   if (cp != NULL && strcasecmp(cp, "no") == 0) {
      km_do_shell = false;
   }
}

int main(int argc, char* argv[])
{
   km_vcpu_t* vcpu = NULL;
   int envc;   // payload env
   char** envp;
   int argc_p;      // payload's argc
   char** argv_p;   // payload's argc (*not* in ABI format)
   char* payload_name;
   char* pl_name;

   km_check_do_shell();

   if (km_do_shell != 0) {
      /*
       * Sometimes km is invoked as a payload name that is a symlink to km.
       * In that case, no km related arguments will be found on the command line.
       * Detect those cases here and help km_trace_setup() avoid treating payload
       * arguments as km arguments.
       * First call works for symlinks, including found in PATH.
       * But for shebang the first is shebang file itself, hence the second call.
       */
      if ((pl_name = km_traverse_payload_symlinks((const char*)getauxval(AT_EXECFN))) == NULL) {
         pl_name = km_traverse_payload_symlinks((const char*)argv[0]);
      }
   } else {
      pl_name = NULL;
   }
   km_trace_setup(argc, argv, pl_name);   // setup trace settings as early as possible
   // Tracing is now enabled.  Log what we decided to run.
   if (pl_name != NULL) {
      km_tracex("Setting payload name to %s", pl_name);
   }

   km_gdbstub_init();

   if (km_exec_recover_kmstate() < 0) {   // exec state is messed up
      km_errx(2, "Problems in performing post exec processing");
   }

   if ((payload_name = km_parse_args(argc, argv, &argc_p, &argv_p, &envc, &envp, pl_name)) == NULL) {
      km_warnx("Failed to determine payload name or find .km file for %s", argv[0]);
      usage();
   }

   km_payload_name = strdup(payload_name);

   km_hcalls_init();
   km_machine_init(&km_machine_init_params);
   km_exec_fini();   // calls to km_called_via_exec() not valid beyond this point!

   km_mgt_init(mgtpipe);

   km_elf_t* elf = km_open_elf_file(payload_name);

   // snapshot file is type ET_CORE. We check for additional notes in restore
   if (elf->ehdr.e_type == ET_CORE) {
      // check for incompatible options
      if (envp != NULL) {
         km_errx(1, "cannot set new environment when resuming a snapshot");
      }
      if (argc_p > 1) {
         km_errx(1, "cannot set payload arguments when resuming a snapshot");
      }
      if (km_snapshot_restore(elf) < 0) {
         km_err(1, "failed to restore from snapshot %s", payload_name);
      }
      vcpu = machine.vm_vcpus[0];
   } else {
      // if environment wasn't set up copy it from host
      if (envp == NULL) {
         for (envc = 0; __environ[envc] != NULL; envc++) {
            ;   // count env vars
         }
         envc++;             // account for terminating NULL
         envp = __environ;   // pointers and strings will be copied to guest stack later
      }
      km_gva_t adjust = km_load_elf(elf);
      if ((vcpu = km_vcpu_get()) == NULL) {
         km_err(1, "Failed to get main vcpu");
      }
      km_gva_t guest_args = km_init_main(vcpu, argc_p, argv_p, envc, envp);
      if (km_dynlinker.km_filename != NULL) {
         if (km_vcpu_set_to_run(vcpu,
                                km_dynlinker.km_ehdr.e_entry + km_dynlinker.km_load_adjust,
                                guest_args) != 0) {
            km_err(1, "failed to set main vcpu to run dynlinker");
         }
      } else {
         if (km_vcpu_set_to_run(vcpu, km_guest.km_ehdr.e_entry + adjust, guest_args) != 0) {
            km_err(1, "failed to set main vcpu to run payload main()");
         }
      }
      if (envp != __environ) {   // if there was no --putenv, we do not need envp array
         for (int i = 0; i < envc; i++) {
            free(envp[i]);
         }
         free(envp);
      }
   }
   km_trace_set_noninteractive();

   if (wait_for_signal != 0) {
      km_warnx("Waiting for kill -SIGUSR1 %d", getpid());
      km_wait_for_signal(SIGUSR1);
   }

   if (km_gdb_is_enabled() != 0) {
      if (km_gdb_setup_listen() == 0) {   // Try to become the gdb server
         if (km_need_pause_all() != 0) {
            km_vcpu_pause_all(vcpu, GUEST_ONLY);   // this just sets machine.pause_requested
         }
         km_gdb_attach_message();
      } else {
         km_warnx("Failed to setup gdb listening port %d, disabling gdb support", gdbstub.port);
         km_gdb_enable(0);   // disable gdb
      }
   }
   km_close_stdio(log_to_fd);

   km_start_vcpus();

   if (km_gdb_is_enabled() != 0) {
      km_gdb_main_loop(vcpu);
      km_gdb_destroy_listen();
   }

   do {
      km_wait_on_eventfd(machine.shutdown_fd);
   } while (km_dofork(NULL) != 0);

   km_machine_fini();
   km_mgt_fini();
   regfree(&km_info_trace.tags);
   exit(machine.exit_status);
}
