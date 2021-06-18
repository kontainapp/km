/*
 * Copyright © 2020-2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "km.h"
#include "km_exec.h"
#include "km_exec_fd_save_recover.h"
#include "km_filesys.h"
#include "km_filesys_private.h"
#include "km_gdb.h"
#include "km_mem.h"

/*
 * These paths in shebang lead to express-parse - see below
 */
static const_string_t SHELL_PATHS[] =
    {"/bin/sh", "/usr/bin/sh", "/bin/ash", "/usr/bin/ash", "/bin/bash", "/usr/bin/bash", NULL};
static const_string_t ENV_PATHS[] = {"/bin/env", "/usr/bin/env", NULL};

/*
 * The execve hypercall builds the following environment variables which are appended to
 * the exec'ed program's environment.  The exec'ed km picks these up and and sets things
 * up for the exec'ed payload.
 * KM state environment variables:
 * KM_EXEC_VERS=1,nfdmap
 * KM_EXEC_VMFDS=kvmfd,machfd,vcpufd,vcpufd,vcpufd,vcpufd,......
 * KM_EXEC_EVENTFDS=intrfd,shutdownfd
 * KM_EXEC_GUESTFDS=gfd:hfd,gfd:hfd,.....
 * KM_EXEC_PIDINFO=tracepid
 * KM_EXEC_GDBINFO=gdbenabled,waitatstarup
 */
#define KM_EXEC_VARS 6
static const int KM_EXEC_VERNUM = 2;
static char KM_EXEC_VERS[] = "KM_EXEC_VERS";
static char KM_EXEC_VMFDS[] = "KM_EXEC_VMFDS";
static char KM_EXEC_EVENTFDS[] = "KM_EXEC_EVENTFDS";
static char KM_EXEC_GUESTFDS[] = "KM_EXEC_GUESTFDS";
static char KM_EXEC_PIDINFO[] = "KM_EXEC_PIDINFO";
static char KM_EXEC_GDBINFO[] = "KM_EXEC_GDBINFO";

typedef struct km_exec_state {
   int version;
   int tracepid;
   int shutdown_fd;
   int intr_fd;
   int kvm_fd;
   int mach_fd;
   int kvm_vcpu_fd[KVM_MAX_VCPUS];
   int nfdmap;
   km_file_t guestfds[0];
} km_exec_state_t;

int km_exec_started_this_payload = 0;

static km_exec_state_t* execstatep;

void km_exec_get_file_pointer(int fd, km_file_t** filep, int* nfds)
{
   if (fd >= execstatep->nfdmap) {
      km_errx(1, "requested fd %d to large, nfdmap %d", fd, execstatep->nfdmap);
   }
   *filep = &execstatep->guestfds[fd];
   if (nfds != NULL) {
      *nfds = execstatep->nfdmap;
   }
}

// The args from main so that exec can rebuild the km command line
static int km_exec_argc;
static char** km_exec_argv;

int km_called_via_exec(void)
{
   return km_exec_started_this_payload;
}

/*
 * Build an "KM_EXEC_VERS=1,xxxx" string to put in the environment for an execve() so that the
 * exec'ed instance of km can know how to put the payload environment back together.
 */
static char* km_exec_vers_var(void)
{
   int bufl = sizeof(KM_EXEC_VERS) + 1 + 4 + sizeof(",fffff");
   char* bufp = malloc(bufl);
   if (bufp != NULL) {
      if (snprintf(bufp, bufl, "%s=%d,%d", KM_EXEC_VERS, KM_EXEC_VERNUM, km_fs_max_guestfd()) + 1 >
          bufl) {
         free(bufp);
         bufp = NULL;
      }
   }
   km_infox(KM_TRACE_EXEC, "version var: %s", bufp);
   return bufp;
}

/*
 * Build "KM_EXEC_GUESTFDS=....." environment variable for exec to pass on the exec'ed program.
 */
static char* km_exec_g2h_var(void)
{
   return km_exec_save_fd(KM_EXEC_GUESTFDS);
}

/*
 * Build "KM_EXEC_VMFDS=....." environment variable for exec to pass on the exec'ed program.
 */
static char* km_exec_vmfd_var(void)
{
   int bufl = sizeof(KM_EXEC_VMFDS) + 1 + (KVM_MAX_VCPUS + 2) * sizeof("xxxxx,");
   char* bufp = malloc(bufl);
   int bytes_avail = bufl;
   char* p = bufp;
   int bytes_needed;

   if (bufp == NULL) {
      return NULL;
   }

   bytes_needed = snprintf(p, bytes_avail, "%s=%d,%d", KM_EXEC_VMFDS, machine.kvm_fd, machine.mach_fd);
   if (bytes_needed + 1 > bytes_avail) {
      free(bufp);
      return NULL;
   }
   p += bytes_needed;
   bytes_avail -= bytes_needed;
   for (int i = 0; i < KVM_MAX_VCPUS && machine.vm_vcpus[i] != NULL; i++) {
      bytes_needed = snprintf(p, bytes_avail, ",%d", machine.vm_vcpus[i]->kvm_vcpu_fd);
      if (bytes_needed + 1 > bytes_avail) {
         free(bufp);
         return NULL;
      }
      p += bytes_needed;
      bytes_avail -= bytes_needed;
   }
   return bufp;
}

/*
 * Build "KM_EXEC_EVENTFDS=....." environment variable for exec to pass on the exec'ed program.
 */
static char* km_exec_eventfd_var(void)
{
   int bufl = sizeof(KM_EXEC_EVENTFDS) + 1 + sizeof("xxxx,yyyy");
   char* bufp = malloc(bufl);
   if (bufp == NULL) {
      return NULL;
   }
   int bytes_needed =
       snprintf(bufp, bufl, "%s=%d,%d", KM_EXEC_EVENTFDS, machine.intr_fd, machine.shutdown_fd);
   if (bytes_needed + 1 > bufl) {
      free(bufp);
      return NULL;
   }
   return bufp;
}

/*
 * Build "KM_EXEC_PIDINFO=....." environment variable for exec to pass on the exec'ed program.
 */
static char* km_exec_pidinfo_var(void)
{
   int bufl = sizeof(KM_EXEC_PIDINFO) + 1 + sizeof("ttttttt");
   char* bufp = malloc(bufl);
   if (bufp == NULL) {
      return NULL;
   }
   int bytes_needed = snprintf(bufp, bufl, "%s=%d", KM_EXEC_PIDINFO, km_trace_include_pid_value());
   if (bytes_needed + 1 > bufl) {
      free(bufp);
      return NULL;
   }
   return bufp;
}

/*
 * Build "KM_EXEC_GDBINFO=...." environment variable for gdb related values that are passed to the
 * new instance of km.
 */
static char* km_exec_gdbinfo_var(void)
{
   int bufl = sizeof(KM_EXEC_GDBINFO) + 1 + (17 * strlen("xxxx,")) + strlen("fs=xxxx,") +
              (MAX_GDB_VFILE_OPEN_FD * strlen("xxxx,"));
   char* bufp = malloc(bufl);
   if (bufp == NULL) {
      return NULL;
   }
   int bytes_needed = snprintf(bufp,
                               bufl,
                               "%s=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,fs=%d,",
                               KM_EXEC_GDBINFO,
                               gdbstub.port,
                               gdbstub.listen_socket_fd,
                               gdbstub.sock_fd,
                               gdbstub.enabled,
                               gdbstub.wait_for_attach,
                               gdbstub.gdb_client_attached,
                               gdbstub.send_threadevents,
                               gdbstub.clientsup_multiprocess,
                               gdbstub.clientsup_xmlregisters,
                               gdbstub.clientsup_qRelocInsn,
                               gdbstub.clientsup_swbreak,
                               gdbstub.clientsup_hwbreak,
                               gdbstub.clientsup_forkevents,
                               gdbstub.clientsup_vforkevents,
                               gdbstub.clientsup_execevents,
                               gdbstub.clientsup_vcontsupported,
                               gdbstub.clientsup_qthreadevents,
                               gdbstub.vfile_state.current_fs);
   if (bytes_needed + 1 > bufl) {
      free(bufp);
      return NULL;
   }
   // Add open gdb file handles to the environment variable
   for (int i = 0; i < MAX_GDB_VFILE_OPEN_FD; i++) {
      int ba = bufl - strlen(bufp);
      int bn = snprintf(bufp + strlen(bufp), ba, "%d,", gdbstub.vfile_state.fd[i]);
      if (bn + 1 > ba) {
         km_infox(KM_TRACE_EXEC, "Out of string space on fd index %d, allocated %d bytes", i, bufl);
         free(bufp);
         return NULL;
      }
   }
   return bufp;
}

/*
 * Called before exec.
 * Append km exec related state to the passed environment and return
 * a pointer to the combined environment.
 * And, append gdb related km variables to the environment.
 */
char** km_exec_build_env(char** envp)
{
   int gdb_vars = 0;
   // Count the passed env
   int envc;
   for (envc = 0; envp[envc] != NULL; envc++) {
   }
   envc++;   // include the null stop pointer

   // The debug related environment variables
   char* fork_wait = getenv(KM_GDB_CHILD_FORK_WAIT);
   char* km_verbose = getenv(KM_VERBOSE);
   if (fork_wait != NULL) {
      gdb_vars++;
   }
   if (km_verbose != NULL) {
      gdb_vars++;
   }
   if (km_do_shell == 0) {
      gdb_vars++;
   }
   km_infox(KM_TRACE_EXEC, "ENV: fork_wait %s, km verbose %s", fork_wait, km_verbose);

   // Allocate a new env array with space for the exec related vars.
   char** newenvp = malloc((envc + gdb_vars + KM_EXEC_VARS) * sizeof(char*));
   if (newenvp == NULL) {
      return NULL;
   }

   // Copy user env vars into new array.
   int i;
   for (i = 0; i < envc - 1; i++) {
      newenvp[i] = km_gva_to_kma((km_gva_t)envp[i]);
      if (newenvp[i] == NULL) {
         free(newenvp);
         return NULL;
      }
      km_infox(KM_TRACE_EXEC, "exec input env[%d] = %s", i, newenvp[i]);
   }

   // env var builder function addresses
   char* (*envvar_build[KM_EXEC_VARS])(void) = {km_exec_vers_var,
                                                km_exec_g2h_var,
                                                km_exec_vmfd_var,
                                                km_exec_eventfd_var,
                                                km_exec_pidinfo_var,
                                                km_exec_gdbinfo_var};

   // Add exec vars to the new env
   int j;
   char* envvarp;
   for (j = 0; j < KM_EXEC_VARS; j++) {
      if ((envvarp = envvar_build[j]()) == NULL) {
         free(newenvp);
         return NULL;
      }
      newenvp[i + j] = envvarp;
      km_infox(KM_TRACE_EXEC, "adding env[%d] %s", i + j, envvarp);
   }
   char envstring[256];
   if (fork_wait != NULL) {
      snprintf(envstring, sizeof(envstring), "%s=%s", KM_GDB_CHILD_FORK_WAIT, fork_wait);
      newenvp[i + j] = strdup(envstring);
      j++;
   }
   if (km_verbose != NULL) {
      snprintf(envstring, sizeof(envstring), "%s=%s", KM_VERBOSE, km_verbose);
      newenvp[i + j] = strdup(envstring);
      j++;
   }
   if (km_do_shell == 0) {
      snprintf(envstring, sizeof(envstring), "%s=%s", KM_DO_SHELL, "NO");
      newenvp[i + j] = strdup(envstring);
      j++;
   }
   newenvp[i + j] = NULL;
   assert(i + j == envc - 1 + KM_EXEC_VARS + gdb_vars);

   // Return pointer to the new env.
   return newenvp;
}

/*
 * Walk through the payload's environment vars to find the requested variable.
 */
static char* km_payload_getenv(const char* varname, char** envp)
{
   size_t vnl = strlen(varname);
   char** varp;
   km_infox(KM_TRACE_ARGS, "varname %s", varname);
   for (varp = envp; *varp != NULL; varp++) {
      char* var_kma = km_gva_to_kma((km_gva_t)*varp);
      km_infox(KM_TRACE_ARGS,
               "var %p, var_kma %p, %s",
               *varp,
               var_kma,
               var_kma != NULL ? var_kma : "bad address");
      assert(var_kma != NULL);
      if (strncmp(var_kma, varname, vnl) == 0 && *(var_kma + vnl) == '=') {
         return var_kma + vnl + 1;
      }
   }
   return NULL;
}

// Resolve the passed command name using the guest's PATH
static char* km_resolve_cmdname(const char* cmdname, const char* path, char* temp, size_t temp_len)
{
   if (*cmdname == '/' || strncmp(cmdname, "./", 2) == 0 || strncmp(cmdname, "../", 3 == 0)) {
      return strncpy(temp, cmdname, temp_len);
   }
   // try path if specified, and the file name isn't full
   if (path != NULL) {
      km_infox(KM_TRACE_EXEC, "Using PATH=%s", path);
      // Parse the value of the PATH var and try to find the path of the payload
      for (const char* pvp = strtok(strdupa(path), ":"); pvp != NULL; pvp = strtok(NULL, ":")) {
         struct stat statbuf;

         snprintf(temp, temp_len, "%s/%s", pvp, cmdname);
         if (stat(temp, &statbuf) == 0 && (statbuf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
            km_infox(KM_TRACE_EXEC, "Resolved cmd %s to path %s", cmdname, temp);
            return temp;
         }
      }
      km_infox(KM_TRACE_EXEC, "couldn't find cmd %s in PATH %s", cmdname, path);
   }
   return NULL;
}

static void freevector(char** sv, int deep)
{
   km_infox(KM_TRACE_EXEC, "freeing arg vector %p", sv);
   if (sv != NULL) {
      if (deep != 0) {
         for (int i = 0; sv[i] != NULL; i++) {
            km_infox(KM_TRACE_EXEC, "freeing %p", sv[i]);
            free(sv[i]);
         }
      }
      free(sv);
   }
}

/*
 * Get a quoted string.  The first char is the quote char. Allow escaped chars in the string.
 * Return a pointer to allocated memory containing the string without the quoting chars at the
 * beginning and end of the string.
 */
static char* get_quoted_string(char** spp)
{
   char* s = *spp;
   char quote = *s;
   char* e = s + 1;

   while (*e != 0) {
      if (*e == '\\') {
         if (*(e + 1) != 0) {
            e += 2;
         } else {
            km_infox(KM_TRACE_EXEC, "backslash followed by string terminator in %s", *spp);
            return NULL;
         }
      } else if (*e != quote) {
         e++;
      } else {   // terminating quote
         char* a = malloc(e - s);
         if (a == NULL) {   // no memory
            return NULL;
         }
         char* d = a;
         char* t = s + 1;
         while (*t != 0 && t < e) {
            if (*t == '\\') {   // skip escaped char marker
               t++;
               assert(*t != 0);
            }
            *d++ = *t++;
         }
         *d = 0;
         *spp = e + 1;
         return a;
      }
   }
   km_infox(KM_TRACE_EXEC, "No terminating quote in %s", *spp);
   return NULL;
}

/*
 * Parse a shell command line and return the arguments in an allocated address vector.  The
 * caller is responsible for freeing the address vector and the memory pointed to by the vector
 * elements. This function will fail if it finds any special shell constructs, like: i/o
 * redirection backgrounding pipelining shell variables regular expressions command evaluation
 * like `echo abc` compound commands (&& ||) Returns: vector address on success NULL on failure
 */
static char** km_parse_shell_cmd_line(char* s, int* cnt)
{
   int svi = 0;
   char** sv = NULL;
   km_infox(KM_TRACE_EXEC, "parsing cmdline %s", s);

   for (;;) {
      // skip leading whitespace
      while (*s != 0 && (*s == ' ' || *s == '\t')) {
         s++;
      }
      if (*s == 0) {   // nothing left
         break;
      }

      char* a;
      if (*s == '"' || *s == '\'') {   // Handle quoted string
         a = get_quoted_string(&s);
      } else {   // whitespace delimited string
         int l;
         char* e = strpbrk(s, " \t");
         if (e == NULL) {
            l = strlen(s);
         } else {
            l = e - s;
         }
         if ((a = malloc(l + 1)) == NULL) {
            km_infox(KM_TRACE_EXEC, "Couldn't allocate %d bytes", l + 1);
            freevector(sv, 1);
            return NULL;
         }
         strncpy(a, s, l);
         a[l] = 0;
         s += l;
      }
      km_infox(KM_TRACE_EXEC, "arg[%d] = %s", svi, a);
      if ((sv = realloc(sv, (svi + 2) * sizeof(char*))) == NULL) {
         km_infox(KM_TRACE_EXEC, "realloc() failed, needed %lu bytes", (svi + 1) * sizeof(char*));
         freevector(sv, 1);
         return NULL;
      }
      sv[svi++] = a;
      sv[svi] = NULL;
      *cnt = svi;
   }

   km_infox(KM_TRACE_EXEC, "returning sv at %p with %d elements", sv, svi);
   return sv;
}

// returns 1 if <name> is in null-terminated <list>
static int km_one_of(const char* name, const_string_t list[])
{
   for (const_string_t* s = list; *s != NULL; s++) {
      if (strncmp(name, *s, PATH_MAX) == 0) {
         km_infox(KM_TRACE_EXEC, "matched: %s", name);
         return 1;
      }
   }
   return 0;
}

int km_is_shell_path(const char* name)
{
   return km_one_of(name, SHELL_PATHS);
}

int km_is_env_path(const char* name)
{
   return km_one_of(name, ENV_PATHS);
}
/*
 * Build an argv[] by taking the current km's args other than the payload args and appending the
 * argv passed to execve().
 *
 * If they are trying to run the shell, we don't want to do that for security reasons. Instead
 * we parse the command line that would be passed to the shell and form a new argv[] which will
 * be appended to the km command line as would be done if a km payload was being exec'ed.
 */
char** km_exec_build_argv(char* filename, char** argv, char** envp)
{
   char** nargv;
   int argc;
   char buf[PATH_MAX];
   int shell;   // indicates special handling for shells and env

   /*
    * Sometimes the callers of exec() supply km itself as the "payload" name.
    * We try to detect this and prevent it from causing an error.
    * Here we toss the first argument, the program to run and use the new
    * first argument as the filename.  We don't yet lookup the new argv[0]
    * if it is a name only (the full path to the program is not supplied).
    * This is a stopgap measure to compensate for dockerfile entrypoints
    * being set to /opt/kontain/bin/km
    */
   char* kmpath = km_get_self_name();
   if (strcmp(filename, kmpath) == 0) {
      km_infox(KM_TRACE_EXEC, "payload name (%s) is km, using %s instead", filename, (char*)km_gva_to_kma((km_gva_t)argv[1]));
      argv++;
      filename = (char*)km_gva_to_kma((km_gva_t)argv[0]);
   }

   // host addresses, just to make less gva_to_kma below
   char* argv0 = (char*)km_gva_to_kma((km_gva_t)argv[0]);
   char* argv1 = (char*)km_gva_to_kma((km_gva_t)argv[1]);
   char* argv2 = (char*)km_gva_to_kma((km_gva_t)argv[2]);
   char* argv3 = (char*)km_gva_to_kma((km_gva_t)argv[3]);

   km_infox(KM_TRACE_EXEC,
            "filename %s, argv[0] %s, argv[1] %s, argv[2] %s, argv[3] %s",
            filename,
            argv0,
            argv1,
            argv2,
            argv3);

   /*
    * If this is popen, build new argv and argc from cmd line.
    * Otherwise compute argc and copy argv from guest address space
    */
   int is_shell = km_is_shell_path(filename);
   int is_env = km_is_env_path(filename);
   if (km_do_shell != 0 && (is_shell == 1 || is_env == 1)) {
      if (argv0 == NULL || argv1 == NULL) {
         km_infox(KM_TRACE_EXEC, "NULL in argv0 (%p) or argv1(%p)", argv0, argv1);
         return NULL;
      }

      char* cmdline;
      if (is_shell == 1 && (argv2 == NULL || argv3 != NULL || strcmp(argv1, "-c") != 0)) {
         km_infox(KM_TRACE_EXEC, "-c required but not found in execve for /bin/sh");
         return NULL;
      }
      if (is_shell == 1) {
         cmdline = argv2;   // skip -c for shell
      } else {
         cmdline = argv1;   // no flags parsing for env, for now
      }

      shell = 1;
      km_infox(KM_TRACE_EXEC, "Parsing shell command: %s", cmdline);
      if ((nargv = km_parse_shell_cmd_line(cmdline, &argc)) == NULL) {
         return NULL;
      }
      filename = km_resolve_cmdname(nargv[0], km_payload_getenv("PATH", envp), buf, sizeof(buf));
      if (filename == NULL) {
         freevector(nargv, 1);
         return NULL;
      }
   } else {
      shell = 0;
      for (argc = 0; argv[argc] != NULL; argc++) {
      }
      if ((nargv = calloc(argc + 1, sizeof(char*))) == NULL) {
         km_infox(KM_TRACE_EXEC, "Couldn't allocate %ld bytes", argc * sizeof(char*));
         return NULL;
      }
      for (int idx = 0; argv[idx] != NULL; idx++) {
         nargv[idx] = km_gva_to_kma((km_gva_t)argv[idx]);
      }
   }
   argv = nargv;

   /*
    * First check for shebang as executable.
    * Note that km_parse_shebang() follows symlinks via open() so symlink to shebang also works.
    */
   char* pl_name;
   char* extra_arg = NULL;   // shebang arg (if any) will be placed here, strdup-ed
   if ((pl_name = km_parse_shebang(filename, &extra_arg)) != NULL) {
      int idx = 1;   // 0 is km executable (assigned below), followed by arguments
      if (extra_arg != NULL) {
         // km exec, shebang arg, shebang, cnt, and NULL
         if ((nargv = calloc(1 + 2 + argc + 1, sizeof(char*))) == NULL) {
            km_infox(KM_TRACE_EXEC, "Couldn't allocate %ld bytes", (1 + 2 + argc + 1) * sizeof(char*));
            freevector(argv, shell);
            return NULL;
         }
         nargv[idx++] = pl_name;
         nargv[idx++] = extra_arg;   // single shebang arg
      } else {
         // km exec, payload, shebang, cnt, and NULL
         if ((nargv = calloc(1 + 1 + argc + 1, sizeof(char*))) == NULL) {
            km_infox(KM_TRACE_EXEC, "Couldn't allocate %ld bytes", (1 + 1 + argc + 1) * sizeof(char*));
            freevector(argv, shell);
            return NULL;
         }
         nargv[idx++] = pl_name;
      }
      nargv[idx++] = filename;   // shebang file

      memcpy(nargv + idx, argv + 1, sizeof(char*) * (argc - 1));   // payload args
      nargv[0] = km_get_self_name();                               // km exec
      return nargv;
   }
   if (km_do_shell != 0) {
      // not shebang, got to be either a symlink or somefile.km
      char* suffix = rindex(filename, '.');
      if (suffix != NULL && strcmp(suffix, PAYLOAD_SUFFIX) == 0) {
         km_infox(KM_TRACE_EXEC, "Setting payload name to %s ", filename);
         pl_name = strdup(filename);
      } else {
         if ((pl_name = km_traverse_payload_symlinks(filename)) == NULL) {
            km_infox(KM_TRACE_EXEC, "km_traverse_payload_symlinks(%s) returned NULL", filename);
            freevector(argv, shell);
            return NULL;
         }
      }
   } else {
      pl_name = strdup(filename);   // not shebang
   }
   if ((nargv = calloc(1 + argc + 1, sizeof(char*))) == NULL) {   // km exec, cnt, and NULL
      km_infox(KM_TRACE_EXEC, "Couldn't allocate %ld bytes", (1 + argc + 1) * sizeof(char*));
      freevector(argv, shell);
      return NULL;
   }
   nargv[0] = km_get_self_name();   // km exec
   nargv[1] = pl_name;
   memcpy(nargv + 2, argv + 1, sizeof(char*) * (argc - 1));

   if (km_trace_tag_enabled(KM_TRACE_EXEC) != 0) {
      for (int i = 0; i < argc + 1; i++) {
         km_infox(KM_TRACE_EXEC, "arg[%d] = %s", i, nargv[i]);
      }
   }
   return nargv;
}

static int km_exec_get_vmfds(char* vmfds)
{
   int n;

   // Get the vm's open fd's
   n = sscanf(vmfds, "%d,%d,", &execstatep->kvm_fd, &execstatep->mach_fd);
   if (n != 2) {
      return -1;
   }
   char* vmfds_copy = strdup(vmfds);
   if (vmfds_copy == NULL) {
      return -1;
   }
   char* pi = vmfds_copy;
   char* saveptr;
   char* p;
   p = strtok_r(pi, ",", &saveptr);
   assert(p != NULL);
   pi = NULL;
   p = strtok_r(pi, ",", &saveptr);
   assert(p != NULL);
   int vcpu_num = 0;
   while ((p = strtok_r(pi, ",", &saveptr)) != NULL) {
      execstatep->kvm_vcpu_fd[vcpu_num] = atoi(p);
      assert(execstatep->kvm_vcpu_fd[vcpu_num] >= 0);
      vcpu_num++;
   }
   execstatep->kvm_vcpu_fd[vcpu_num] = -1;
   free(vmfds_copy);

   return 0;
}

/*
 * Parse the contents of the guestfds environment variable into the execstate memory where
 * it can be used to restore the guestfd state in km_filesys.
 * We want to weed out parse errors here before we get into km initialization.
 */
static int km_exec_get_guestfds(char* guestfds)
{
   // Get the guest to host fd map, note: strtok clobbers the input string
   char* guestfds_copy = strdup(guestfds);
   if (guestfds_copy == NULL) {
      return -1;
   }
   int rc = km_exec_restore_fd(guestfds_copy);
   free(guestfds_copy);
   return rc;
}

/*
 * Called in the exec'ed program before setting up the vm to get rid of open file descriptors
 * from exec. If we mark these fd's as close on exec, we wouldn't need to do this.
 */
static void km_exec_vmclean(void)
{
   assert(execstatep != NULL);
   close(execstatep->intr_fd);
   close(execstatep->shutdown_fd);
   close(execstatep->kvm_fd);
   close(execstatep->mach_fd);
   for (int i = 0; i < KVM_MAX_VCPUS && execstatep->kvm_vcpu_fd[i] >= 0; i++) {
      close(execstatep->kvm_vcpu_fd[i]);
   }
}

// Called before running the execed program.  Picks up what was saved by km_exec_save_kmstate()
int km_exec_recover_kmstate(void)
{
   char* vernum = getenv(KM_EXEC_VERS);
   char* vmfds = getenv(KM_EXEC_VMFDS);
   char* eventfds = getenv(KM_EXEC_EVENTFDS);
   char* guestfds = getenv(KM_EXEC_GUESTFDS);
   char* pidinfo = getenv(KM_EXEC_PIDINFO);
   char* gdbinfo = getenv(KM_EXEC_GDBINFO);
   int version;
   int nfdmap;
   int n;

   km_infox(KM_TRACE_EXEC, "recovering km exec state, vernum: %s", vernum != NULL ? vernum : "parent");

   if (vernum == NULL || vmfds == NULL || eventfds == NULL || guestfds == NULL || pidinfo == NULL) {
      // If we don't have them all, then this isn't an exec().
      // And, if one is missing they all need to be missing.
      assert(vernum == NULL && vmfds == NULL && eventfds == NULL && guestfds == NULL && pidinfo == NULL);
      return 0;
   }

   km_exec_started_this_payload = 1;
   if ((n = sscanf(vernum, "%d,%d", &version, &nfdmap)) != 2) {
      km_infox(KM_TRACE_EXEC, "couldn't process vernum %s, n %d", vernum, n);
      return -1;
   }
   if (version != KM_EXEC_VERNUM) {
      // Hmmm, did they install a new version of km while we were running?
      km_infox(KM_TRACE_EXEC, "exec state verion mismatch, got %d, expect %d", version, KM_EXEC_VERNUM);
      return -1;
   }
   if ((execstatep = calloc(1, sizeof(*execstatep) + (sizeof(km_file_t) * nfdmap))) == NULL) {
      km_infox(KM_TRACE_EXEC, "couldn't allocate fd map, nfdmap %d", nfdmap);
      return -1;
   }
   // Get the pid information
   if ((n = sscanf(pidinfo, "%d", &execstatep->tracepid)) != 1) {
      km_infox(KM_TRACE_EXEC, "couldn't scan pidinfo %s, n %d", pidinfo, n);
      return -1;
   }
   execstatep->version = version;
   execstatep->nfdmap = nfdmap;
   km_trace_include_pid(execstatep->tracepid);
   // Tracing should be ok from this point on.

   if (km_exec_get_vmfds(vmfds) != 0) {
      return -1;
   }

   // Get the event fd's
   if ((n = sscanf(eventfds, "%d,%d", &execstatep->intr_fd, &execstatep->shutdown_fd)) != 2) {
      km_infox(KM_TRACE_EXEC, "couldn't get event fd's %s, n %d", eventfds, n);
      return -1;
   }

   if (km_exec_get_guestfds(guestfds) != 0) {
      km_infox(KM_TRACE_EXEC, "couldn't parse guestfds %s", guestfds);
      return -1;
   }

   // Get the gdb state back.  Not sure if gdb expects us to remember open gdb fd's.
   int wait_for_attach;
   n = sscanf(gdbinfo,
              "%d,%d,%d,%hhd,%d,%hhd,%d,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,fs=%d,",
              &gdbstub.port,
              &gdbstub.listen_socket_fd,
              &gdbstub.sock_fd,
              &gdbstub.enabled,
              &wait_for_attach,
              &gdbstub.gdb_client_attached,
              &gdbstub.send_threadevents,
              &gdbstub.clientsup_multiprocess,
              &gdbstub.clientsup_xmlregisters,
              &gdbstub.clientsup_qRelocInsn,
              &gdbstub.clientsup_swbreak,
              &gdbstub.clientsup_hwbreak,
              &gdbstub.clientsup_forkevents,
              &gdbstub.clientsup_vforkevents,
              &gdbstub.clientsup_execevents,
              &gdbstub.clientsup_vcontsupported,
              &gdbstub.clientsup_qthreadevents,
              &gdbstub.vfile_state.current_fs);
   if (n != 18) {
      km_infox(KM_TRACE_EXEC, "couldn't get gdb info, n %d, gdbinfo %s", n, gdbinfo);
      return -1;
   }

   // Get the open gdb file descriptors
   char* s = strstr(gdbinfo, "fs=");
   s = strstr(s, ",");
   s++;
   for (int i = 0; i < MAX_GDB_VFILE_OPEN_FD; i++) {
      gdbstub.vfile_state.fd[i] = atoi(s);
      s = strstr(s, ",");
      s++;
   }

   /*
    * Remove our km state recovery variables from the environment.
    * If we don't they appear in the environment if this process
    * performs an exec().
    */
   unsetenv(KM_EXEC_VERS);
   unsetenv(KM_EXEC_VMFDS);
   unsetenv(KM_EXEC_EVENTFDS);
   unsetenv(KM_EXEC_GUESTFDS);
   unsetenv(KM_EXEC_PIDINFO);
   unsetenv(KM_EXEC_GDBINFO);

   gdbstub.wait_for_attach = wait_for_attach;
   if (gdbstub.enabled != 0 && gdbstub.gdb_client_attached != 0) {
      // tell the gdb client we are not debugging the same executable
      gdbstub.send_exec_event = 1;
   }
   km_exec_vmclean();

   km_infox(KM_TRACE_EXEC, "km exec state recovered successfully");
   return 1;
}

/*
 * Rebuild the guestfd to hostfd maps.
 * Should be called from km_fs_init() to reinstate the guest fd's.
 * Returns:
 *   0 - guest fd recover happened.
 *   1 - no guest fd recovery required.
 */
int km_exec_recover_guestfd(void)
{
   if (execstatep == NULL) {
      return 1;
   }
   for (int i = 0; i < execstatep->nfdmap; i++) {
      if (execstatep->guestfds[i].inuse == 0) {
         continue;
      }

      km_file_t* file = &km_fs()->guest_files[i];
      *file = execstatep->guestfds[i];
      // Fixup a few things
      TAILQ_INIT(&file->events);
      TAILQ_CONCAT(&file->events, &execstatep->guestfds[i].events, link);

      km_exec_fdtrace("after exec", i);

      // Remove pointers from the source km_file_t
      execstatep->guestfds[i].name = NULL;
      execstatep->guestfds[i].sockinfo = NULL;
      TAILQ_INIT(&execstatep->guestfds[i].events);
   }
   return 0;
}

void km_exec_init_args(int argc, char** argv)
{
   // Remember this in case the guest performs an execve().
   km_exec_argc = argc;   // km-specific argc ... all args after that belong to payload
   km_exec_argv = argv;
}

void km_exec_fini(void)
{
   free(execstatep);
   execstatep = NULL;
}
