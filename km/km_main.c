/*
 * Copyright © 2018-2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

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

#include "km.h"
#include "km_coredump.h"
#include "km_elf.h"
#include "km_exec.h"
#include "km_fork.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_snapshot.h"

km_info_trace_t km_info_trace;

extern int vcpu_dump;

static inline void usage()
{
   errx(1,
        "Kontain Monitor - runs 'payload-file [payload args]' in Kontain VM\n"
        "Usage: km [options] payload-file[.km] [payload_args ... ]\n"

        "\nOptions:\n"
        "\t--verbose[=regexp] (-V[regexp])     - Verbose print where internal info tag matches "
        "'regexp'\n"
        "\t--gdb-server-port[=port] (-g[port]) - Enable gbd server listening on <port> (default "
        "2159)\n"
        "\t--gdb-listen                        - gdb server listens for client while payload runs\n"
        "\t--gdb-dynlink                       - gdb server waits for client attach before dyn "
        "link runs\n"
        "\t--version (-v)                      - Print version info and exit\n"
        "\t--log-to=file_name                  - Stream stdout and stderr to file_name\n"
        "\t--copyenv                           - Copy all KM env. variables into payload "
        "(default)\n"
        "\t--putenv key=value                  - Add environment 'key' to payload (cancels "
        "--copyenv)\n"
        "\t--wait-for-signal                   - Wait for SIGUSR1 before running payload\n"
        "\t--dump-shutdown                     - Produce register dump on VCPU error\n"
        "\t--core-on-err                       - generate KM core dump when exiting on err, "
        "including guest core dump\n"
        "\t--overcommit-memory                 - Allow huge address allocations for payloads.\n"
        "\t                                      See 'sysctl vm.overcommit_memory'\n"
        "\t--dynlinker=file_name               - Set dynamic linker file (default: "
        "/opt/kontain/lib64/libc.so)\n"
        "\t--hcall-stats (-S)                  - Collect and print hypercall stats\n"
        "\t--coredump=file_name                - File name for coredump\n"
        "\t--snapshot=file_name                - File name for snapshot\n"
        "\t--resume                            - Resume from a snapshot\n"

        "\n\tOverride auto detection:\n"
        "\t--membus-width=size (-Psize)        - Set guest physical memory bus size in bits, i.e. "
        "32 means 4GiB, 33 8GiB, 34 16GiB, etc. \n"
        "\t--enable-1g-pages                   - Force enable 1G pages support (default). Assumes "
        "hardware support\n"
        "\t--disable-1g-pages                  - Force disable 1G pages support\n"
        "\t--use-kvm                           - Use kvm driver\n"
        "\t--use-kkm                           - Use kkm driver");
}

// Version info. SCM_* is supposed to be set by the build
static const int ver_major = 0;
static const int ver_minor = 3;
#ifndef SRC_BRANCH   // branch name
#define SRC_BRANCH "?"
#endif
#ifndef SRC_VERSION   // SHA
#define SRC_VERSION "?"
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "?"
#endif

static inline void show_version(void)
{
   errx(0,
        "Kontain Monitor version %d.%d\nBranch: %s sha: %s build_time: %s",
        ver_major,
        ver_minor,
        SRC_BRANCH,
        SRC_VERSION,
        BUILD_TIME);
}

// Option names we use elsewhere.
#define GDB_LISTEN "gdb-listen"
#define GDB_DYNLINK "gdb-dynlink"

km_machine_init_params_t km_machine_init_params = {
    .force_pdpe1g = KM_FLAG_FORCE_ENABLE,
    .overcommit_memory = KM_FLAG_FORCE_DISABLE,
    .use_virt = KM_FLAG_FORCE_DEFAULT,
};
static int wait_for_signal = 0;
int debug_dump_on_err = 0;   // if 1, will abort() instead of err()
static int resume_snapshot = 0;
static struct option long_options[] = {
    {"wait-for-signal", no_argument, &wait_for_signal, 1},
    {"dump-shutdown", no_argument, 0, 'D'},
    {"enable-1g-pages", no_argument, &(km_machine_init_params.force_pdpe1g), KM_FLAG_FORCE_ENABLE},
    {"disable-1g-pages", no_argument, &(km_machine_init_params.force_pdpe1g), KM_FLAG_FORCE_DISABLE},
    {"overcommit-memory", no_argument, &(km_machine_init_params.overcommit_memory), KM_FLAG_FORCE_ENABLE},
    {"coredump", required_argument, 0, 'C'},
    {"membus-width", required_argument, 0, 'P'},
    {"log-to", required_argument, 0, 'l'},
    {"putenv", required_argument, 0, 'e'},
    {"copyenv", no_argument, 0, 'E'},
    {"gdb-server-port", optional_argument, 0, 'g'},
    {GDB_LISTEN, no_argument, NULL, 0},
    {GDB_DYNLINK, no_argument, NULL, 0},
    {"verbose", optional_argument, 0, 'V'},
    {"core-on-err", no_argument, &debug_dump_on_err, 1},
    {"version", no_argument, 0, 'v'},
    {"dynlinker", required_argument, 0, 'L'},
    {"hcall-stats", no_argument, 0, 'S'},
    {"use-kvm", no_argument, &(km_machine_init_params.use_virt), KM_FLAG_FORCE_KVM},
    {"use-kkm", no_argument, &(km_machine_init_params.use_virt), KM_FLAG_FORCE_KKM},
    {"snapshot", required_argument, 0, 's'},
    {"resume", no_argument, &resume_snapshot, 1},

    {0, 0, 0, 0},
};

static const_string_t SHEBANG = "#!";
static const size_t SHEBANG_LEN = 2;               // strlen(SHEBANG)
static const size_t SHEBANG_MAX_LINE_LEN = 1000;   // note: grabbed on stack

static const_string_t KM_BIN_NAME = "km";
static const_string_t PAYLOAD_SUFFIX = ".km";

/*
 * Checks if the passed file is a shebang, and if it is gets payload file name from there.
 * Either way fills the passed buffer with the payload file name.
 * Returns 0 on success, -1 on failure
 *
 * TODO: we also need to fetch the rest of the shebang line, and insert it to the payload args
 */
static int km_get_payload_name(char* payload_file, char fname_buf[])
{
   char line_buf[SHEBANG_MAX_LINE_LEN + 1];
   int fd;
   int count;

   if ((fd = open(payload_file, O_RDONLY, 0)) < 0) {
      return -1;
   }
   count = pread(fd, line_buf, SHEBANG_MAX_LINE_LEN, 0);
   close(fd);

   if (count < SHEBANG_LEN) {
      km_err_msg(errno, "Failed to read %s", payload_file);   // failed to read even 2 bytes...
      return -1;
   }

   line_buf[count] = 0;   // null terminate the string
   if (strncmp(line_buf, SHEBANG, SHEBANG_LEN) == 0) {
      km_tracex("Extracting payload name from shebang %s", payload_file);
      char* c = strpbrk(line_buf + SHEBANG_LEN, " \t\n\0");   // find a whitespace or string end
      if (c != NULL) {
         *c = 0;   // null terminate the string
         payload_file = line_buf + SHEBANG_LEN;
         km_tracex("Using payload from '%s'", payload_file);
      } else {
         km_tracex("Ignoring shebang due to wrong format: '%s'", line_buf);
      }
   }

   strncpy(fname_buf, payload_file, PATH_MAX);
   return 0;
}

// parses args, returns index of payload name in argv[]
static int km_parse_args(int argc, char* argv[], char** envp_p[], int* envc_p)
{
   int opt;
   uint64_t port;
   int gpbits = 0;         // Width of guest physical memory bus.
   int copyenv_used = 1;   // By default copy environment from host
   int putenv_used = 0;
   int dynlinker_used = 0;
   char* ep = NULL;
   int regex_flags = (REG_ICASE | REG_NOSUB | REG_EXTENDED);
   int longopt_index;                        // flag index in longopt array
   int payload_index;                        // payload_name index in argv array
   char** envp = calloc(1, sizeof(char*));   // NULL terminated array of env pointers
   int envc = 1;                             // count of elements in envp (including NULL)

   assert(envp != NULL);

   // Special check for tracing
   // TODO: handle generic KM_CLI_FLAGS here, and reuse code below
   char* trace_regex = getenv("KM_VERBOSE");
   if (trace_regex != NULL) {
      km_info_trace.level = KM_TRACE_INFO;
      if (*trace_regex == 0) {
         trace_regex = ".*";   // trace all if regex is not passed
      }
      regcomp(&km_info_trace.tags, trace_regex, regex_flags);
   }

   if (strcmp(basename(argv[0]), KM_BIN_NAME) != 0) {   // Called on behalf of a payload
      // Form the full path to payload and load it, pass args as is but do form the env
      char* payload_name = calloc(1, strlen(argv[0]) + strlen(PAYLOAD_SUFFIX) + 1);
      assert(payload_name != NULL);   // no need to continue if we are already OOM
      sprintf(payload_name, "%s%s", argv[0], PAYLOAD_SUFFIX);
      argv[0] = payload_name;
      payload_index = 0;
      km_tracex("Setting payload name to %s", payload_name);
   } else {   // regular KM invocation - parse KM args
      while ((opt = getopt_long(argc, argv, "+g::e:AEV::P:vC:S", long_options, &longopt_index)) != -1) {
         switch (opt) {
            case 0:
               // If this option set a flag, do nothing else now.
               if (long_options[longopt_index].flag != 0) {
                  break;
               }
               // Put here handling of longopts which do not have related short opt
               if (strcmp(long_options[longopt_index].name, GDB_LISTEN) == 0) {
                  gdbstub.wait_for_attach = GDB_DONT_WAIT_FOR_ATTACH;
                  km_gdb_enable(1);
               } else if (strcmp(long_options[longopt_index].name, GDB_DYNLINK) == 0) {
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
                     km_err_msg(0, "Invalid gdb port number '%s'", optarg);
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
               if (freopen(optarg, "a", stdout) == NULL || freopen(optarg, "a", stderr) == NULL) {
                  err(1, optarg);
               }
               break;
            case 'e':                    // --putenv
               if (copyenv_used > 1) {   // if --copyenv was on the command line, something is wrong
                  warnx("Wrong options: '--putenv' cannot be used with together with '--copyenv'");
                  usage();
               }
               copyenv_used = 0;   // --putenv cancels 'default' --copyenv
               putenv_used++;
               envp[envc - 1] = optarg;
               envc++;
               if ((envp = realloc(envp, sizeof(char*) * envc)) == NULL) {
                  err(1, "Failed to alloc memory for putenv %s", optarg);
               }
               envp[envc - 1] = NULL;
               break;
            case 'E':   // --copyenv
               if (putenv_used != 0) {
                  warnx("Wrong options: '--copyenv' cannot be used with together with '--putenv'");
                  usage();
               }
               copyenv_used++;
               break;
            case 'C':
               km_set_coredump_path(optarg);
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
                  warnx("Wrong memory bus size '%s'", optarg);
                  usage();
               }
               if (gpbits < 32 || gpbits >= 63) {
                  warnx("Guest memory bus width must be between 32 and 63 - got '%d'", gpbits);
                  usage();
               }
               km_machine_init_params.guest_physmem = 1UL << gpbits;
               if (km_machine_init_params.guest_physmem > GUEST_MAX_PHYSMEM_SUPPORTED) {
                  warnx("Guest physical memory must be < 0x%lx - got 0x%lx (bus width %d)",
                        GUEST_MAX_PHYSMEM_SUPPORTED,
                        km_machine_init_params.guest_physmem,
                        gpbits);
                  usage();
               }
               break;
            case 'V':
               if (optarg == NULL) {
                  regcomp(&km_info_trace.tags, ".*", regex_flags);
               } else if (regcomp(&km_info_trace.tags, optarg, regex_flags) != 0) {
                  warnx("Failed to compile -V regexp '%s'", optarg);
                  usage();
               }
               km_info_trace.level = KM_TRACE_INFO;
               break;
            case 'v':
               show_version();
               break;
            case 'L':
               km_dynlinker_file = optarg;
               dynlinker_used++;
               break;
            case 'S':
               km_collect_hc_stats = 1;
               break;
            case ':':
               printf("Missing arg for %c\n", optopt);
               usage();
            default:
               usage();
         }
      }
      payload_index = optind;

      if (resume_snapshot != 0 && copyenv_used == 1) {
         copyenv_used = 0;   // when resuming snapshot, default behavior is NOT to copy env
      }
      if (resume_snapshot != 0 && (putenv_used != 0 || copyenv_used != 0 || dynlinker_used != 0)) {
         km_err_msg(0, "cannot set new environment or dynlinker when resuming a snapshot");
         err(1, "exiting...");
      }
   }
   if (copyenv_used != 0) {       // copy env from host
      assert(putenv_used == 0);   // TODO - we can merge existing and --putenv here
      for (envc = 0; __environ[envc] != NULL; envc++) {
         ;   // count env vars
      }
      envc++;             // account for terminating NULL
      envp = __environ;   // pointers and strings will be copied to guest stack later
   }
   *envp_p = envp;
   *envc_p = envc;
   return payload_index;
}

int main(int argc, char* argv[])
{
   km_vcpu_t* vcpu = NULL;
   char** envp;
   int envc;
   int index;   // index of payload_name in argv

   km_gdbstub_init();
   index = km_parse_args(argc, argv, &envp, &envc);
   if (index == argc) {   // no payload file
      usage();
   }
   char* payload_file = argv[index];

   km_hcalls_init();
   km_exec_init(index, (char**)argv);
   km_machine_init(&km_machine_init_params);
   km_exec_fini();

   if (resume_snapshot != 0) {   // snapshot restore
      if (km_snapshot_restore(payload_file) < 0) {
         err(1, "failed to restore from snapshot %s", payload_file);
      }
      vcpu = machine.vm_vcpus[0];
   } else {   // new guest start
      // first check if it's a shebang and find the correct payload name
      char fname_buf[PATH_MAX + 1];
      if (km_get_payload_name(payload_file, fname_buf) != 0) {
         err(1, "Failed to get path for %s", payload_file);
      }
      km_gva_t adjust = km_load_elf(fname_buf);
      if ((vcpu = km_vcpu_get()) == NULL) {
         err(1, "Failed to get main vcpu");
      }
      km_gva_t guest_args = km_init_main(vcpu, argc - index, argv + index, envc, envp);
      if (km_dynlinker.km_filename != NULL) {
         if (km_vcpu_set_to_run(vcpu,
                                km_dynlinker.km_ehdr.e_entry + km_dynlinker.km_load_adjust,
                                guest_args) != 0) {
            err(1, "failed to set main vcpu to run dynlinker");
         }
      } else {
         if (km_vcpu_set_to_run(vcpu, km_guest.km_ehdr.e_entry + adjust, guest_args) != 0) {
            err(1, "failed to set main vcpu to run payload main()");
         }
      }
      if (envp != __environ) {   // if there was no --putenv, we do not need envp array
         free(envp);
      }
   }

   if (wait_for_signal == 1) {
      warnx("Waiting for kill -SIGUSR1  %d", getpid());
      km_wait_for_signal(SIGUSR1);
   }

   if (km_gdb_is_enabled() != 0) {
      if (km_gdb_setup_listen() == 0) {   // Try to become the gdb server
         km_vcpu_pause_all();
      } else {
         km_err_msg(0, "Failed to setup gdb listening port %d, disabling gdb support", gdbstub.port);
         km_gdb_enable(0);   // disable gdb
      }
   }

   if (km_start_vcpus() < 0) {
      err(2, "Failed to start guest");
   }

   if (km_gdb_is_enabled() == 1) {
      km_gdb_main_loop(vcpu);
      km_gdb_destroy_listen();
   }

   do {
      km_wait_on_eventfd(machine.shutdown_fd);
   } while (km_dofork(NULL) != 0);

   km_machine_fini();
   regfree(&km_info_trace.tags);
   exit(machine.exit_status);
}
