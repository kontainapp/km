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
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "km.h"
#include "km_coredump.h"
#include "km_elf.h"
#include "km_gdb.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_guest.h"
#include "km_exec.h"
#include "km_fork.h"

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
        "\t--putenv key=value                  - Add environment 'key' to payload\n"
        "\t--copyenv                           - Copy all KM env. variables into payload\n"
        "\t--wait-for-signal                   - Wait for SIGUSR1 before running payload\n"
        "\t--dump-shutdown                     - Produce register dump on VCPU error\n"
        "\t--core-on-err                       - generate KM core dump when exiting on err, "
        "including guest core dump\n"
        "\t--overcommit-memory                 - Allow huge address allocations for payloads.\n"
        "\t                                      See 'sysctl vm.overcommit_memory'\n"
        "\t--dynlinker=file_name               - Set dynamic linker file (default: "
        "/opt/kontain/lib64/libc.so)\n"
        "\t--hcall-stats (-S)                  - Collect and print hypercall stats\n"

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

    {0, 0, 0, 0},
};

int main(int argc, char* const argv[])
{
   int opt;
   uint64_t port;
   km_vcpu_t* vcpu;
   char* payload_file;
   int gpbits = 0;   // Width of guest physical memory bus.
   char* ep = NULL;
   int regex_flags = (REG_ICASE | REG_NOSUB | REG_EXTENDED);
   int longopt_index;
   char** envp = calloc(1, sizeof(char*));   // NULL terminated array of env pointers
   int envc = 1;                             // count of elements in envp (including NULL)
   int putenv_used = 0, copyenv_used = 0;    // they are mutually exclusive

   km_gdbstub_init();

   assert(envp != NULL);
   while ((opt = getopt_long(argc, argv, "+g::AV::P:vC:S", long_options, &longopt_index)) != -1) {
      switch (opt) {
         case 0:
            if (strcmp(long_options[longopt_index].name, GDB_LISTEN) == 0) {
               gdbstub.wait_for_attach = GDB_DONT_WAIT_FOR_ATTACH;
               km_gdb_enable(1);
            } else if (strcmp(long_options[longopt_index].name, GDB_DYNLINK) == 0) {
               gdbstub.wait_for_attach = GDB_WAIT_FOR_ATTACH_AT_DYNLINK;
               km_gdb_enable(1);
            }
            /* If this option set a flag, do nothing else now. */
            if (long_options[longopt_index].flag != 0) {
               break;
            }
            // Put here handling of longopts which do not have related short opt
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
            if (gdbstub.wait_for_attach ==
                GDB_WAIT_FOR_ATTACH_UNSPECIFIED) {   // wait at _start by default
               gdbstub.wait_for_attach = GDB_WAIT_FOR_ATTACH_AT_START;
            }
            break;
         case 'l':
            if (freopen(optarg, "a", stdout) == NULL || freopen(optarg, "a", stderr) == NULL) {
               err(1, optarg);
            }
            break;
         case 'e':   // --putenv
            putenv_used++;
            if (copyenv_used != 0) {
               warnx("Wrong options: '--copyenv' cannot be used with together with '--putenv'");
               usage();
            }
            envp[envc - 1] = optarg;
            envc++;
            if ((envp = realloc(envp, sizeof(char*) * envc)) == NULL) {
               err(1, "Failed to alloc memory for putenv %s", optarg);
            }
            envp[envc - 1] = NULL;
            break;
         case 'E':   // --copyenv
            if (copyenv_used++ != 0) {
               warnx("Ignoring redundant '--copyenv' option");
               break;
            }
            if (putenv_used != 0) {
               warnx("Wrong options: '--copyenv' cannot be used with together with '--putenv'");
               usage();
            }
            for (envc = 0; __environ[envc] != NULL; envc++) {
               ;   // count env vars
            }
            envc++;             // account for terminating NULL
            envp = __environ;   // pointers and strings will be copied to guest stack later
            break;
         case 'C':
            km_set_coredump_path(optarg);
            break;
         case 'D':
            vcpu_dump = 1;
            break;
         case 'P':
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
            break;
         case 'S':
            km_collect_hc_stats = 1;
            break;
         case '?':
         default:
            usage();
      }
   }
   if (optind == argc) {
      usage();
   }
   payload_file = argv[optind];
   char** localargv = (char**)&argv[optind];
   int localargc = argc - optind;
   char** localenvp = envp;
   int localenvc = envc;
   int elf_fd = -1;

   km_hcalls_init();
   km_machine_init(&km_machine_init_params);
   km_init_guest_idt();
   if ((vcpu = km_vcpu_get()) == NULL) {
      err(1, "Failed to get main vcpu");
   }
   km_init_syscall_handler(vcpu);

load_again:;
   km_gva_t adjust = km_load_elf(payload_file, elf_fd);
   km_gva_t guest_args = km_init_main(vcpu, localargc, localargv, localenvc, localenvp);
   if (km_exec_state.exec_in_progress != 0) {
      free(km_exec_state.argp_envp);
      km_exec_state.argp_envp = NULL;
      km_exec_state.exec_in_progress = 0;
      assert(vcpu->is_used == 0);
      vcpu->is_used = 1;
   }
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
      if (envp != NULL) {
         free(envp);
         envp = NULL;
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

   km_setup_signal_propagate();
   if (km_run_vcpu_thread(vcpu, km_vcpu_run_main) < 0) {
      err(2, "Failed to create main run_vcpu thread");
   }
   if (km_gdb_is_enabled() == 1) {
      km_gdb_main_loop(vcpu);
      km_gdb_destroy_listen();
      if (km_fork_state.fork_in_progress != 0) {
         // We are the child process.  Get the child vm running.
         km_reinit_for_fork_child();
         km_fork_state.fork_in_progress = 0;
      }
   }

   // Wait for payload termination or execve() hypercall or the child side of a fork() hypercall
wait_for_child_to_exit:;
   km_wait_on_eventfd(machine.shutdown_fd);
   km_infox(KM_TRACE_PROC, "fork in progress %d", km_fork_state.fork_in_progress);
   if (km_fork_state.fork_in_progress != 0) {
      if (km_dofork() == 0) {  //child process
         km_reinit_for_fork_child();
      }
      km_fork_state.fork_in_progress = 0;
      goto wait_for_child_to_exit;
   }

   km_infox(KM_TRACE_PROC, "exec in progress %d", km_exec_state.exec_in_progress);
   if (km_exec_state.exec_in_progress != 0) {  // not really terminated, payload exec()'ed to another executable
      km_infox(KM_TRACE_PROC, "start exec processing, file %s, fd %d", km_exec_state.filename, km_exec_state.fd);
      localargc = km_exec_state.argc;
      localargv = km_exec_state.argp;
      localenvc = km_exec_state.envc + 1;      // include the null pointer, the dyn linker depends on this
      localenvp = km_exec_state.envp;
      payload_file = km_exec_state.filename;
      elf_fd = km_exec_state.fd;
      wait_for_signal = 0;
      machine.park_all = 0;
      machine.pause_requested = 0;
      km_reinit_for_exec();
      goto load_again;
   }

   km_machine_fini();
   km_infox(KM_TRACE_PROC, "exit status %d", machine.exit_status);
   regfree(&km_info_trace.tags);
   exit(machine.exit_status);
}
