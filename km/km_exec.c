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

#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include "km.h"
#include "km_exec.h"
#include "km_filesys.h"
#include "km_mem.h"

/*
 * The execve hypercall builds the following environment variables which are appended to
 * the exec'ed program's environment.  The exec'ed km picks these up and and sets things
 * up for the exec'ed payload.
 * KM state environment variables:
 * KM_EXEC_VERS=1,nfdmap
 * KM_EXEC_VMFDS=kvmfd,machfd,vcpufd,vcpufd,vcpufd,vcpufd,......
 * KM_EXEC_EVENTFDS=intrfd,shutdownfd
 * KM_EXEC_GUESTFDS=gfd:hfd,gfd:hfd,.....
 * KM_EXEC_PIDINFO=tracepid,parentpid,mypid,nextpid
 */
#define KM_EXEC_VARS 5
static const int KM_EXEC_VERNUM = 1;
static char KM_EXEC_VERS[] = "KM_EXEC_VERS";
static char KM_EXEC_VMFDS[] = "KM_EXEC_VMFDS";
static char KM_EXEC_EVENTFDS[] = "KM_EXEC_EVENTFDS";
static char KM_EXEC_GUESTFDS[] = "KM_EXEC_GUESTFDS";
static char KM_EXEC_PIDINFO[] = "KM_EXEC_PIDINFO";

/*
 * An instance of this holds all of the exec state that the execing program puts
 * into the above environment variables.  The exec'ed instance of km retrieves the
 * environment variables and builds an instance of this structure which is then
 * used to restore needed km state for the new payload to continue.
 */
typedef struct fdmap {
   int guestfd;
   int hostfd;
} fdmap_t;

typedef struct km_exec_state {
   int version;
   int tracepid;
   pid_t ppid;
   pid_t pid;
   pid_t next_pid;
   int shutdown_fd;
   int intr_fd;
   int kvm_fd;
   int mach_fd;
   int kvm_vcpu_fd[KVM_MAX_VCPUS];
   int nfdmap;
   fdmap_t guestfd_hostfd[0];
} km_exec_state_t;

static km_exec_state_t* execstatep;

// The args from main so that exec can rebuild the km command line
static int km_exec_argc;
static char** km_exec_argv;
char** km_exec_payload_env;

/*
 * Build an "KM_EXEC_VERS=1,xxxx" string to put in the environment for an
 * execve() so that the exec'ed instance of km can know how to put the
 * payload environment back together.
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
   return bufp;
}

/*
 * Build "KM_EXEC_GUESTFDS=....." environment variable for exec to pass on the exec'ed program.
 */
static char* km_exec_g2h_var(void)
{
   int bufl = sizeof(KM_EXEC_GUESTFDS) + 1 + km_fs_max_guestfd() * sizeof("gggg:hhhh,");
   char* bufp = malloc(bufl);
   int bytes_avail = bufl;
   char* p = bufp;
   int bytes_needed;
   int fdcount = 0;

   if (bufp == NULL) {
      return NULL;
   }
   bytes_needed = snprintf(p, bytes_avail, "%s=", KM_EXEC_GUESTFDS);
   if (bytes_needed + 1 > bufl) {
      free(bufp);
      return NULL;
   }
   bytes_avail -= bytes_needed;
   p += bytes_needed;

   for (int i = 0; i < km_fs_max_guestfd(); i++) {
      int hostfd = km_fs_g2h_fd(i);
      if (hostfd >= 0) {
         int fdflags = fcntl(hostfd, F_GETFD);
         if (fdflags < 0) {
            km_info(KM_TRACE_EXEC, "fcntl() on guestfd %d failed, ignoring it, ", i);
            continue;
         }
         if ((fdflags & FD_CLOEXEC) != 0) {
            continue;
         }
         bytes_needed = snprintf(p, bytes_avail, fdcount == 0 ? "%d:%d" : ",%d:%d", i, hostfd);
         if (bytes_needed + 1 > bytes_avail) {
            free(bufp);
            return NULL;
         }
         fdcount++;
         bytes_avail -= bytes_needed;
         p += bytes_needed;
      }
   }
   return bufp;
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
   int bufl = sizeof(KM_EXEC_PIDINFO) + 1 + sizeof("ttttttt,pppppppp,mmmmmmmm,nnnnnnnn");
   char* bufp = malloc(bufl);
   if (bufp == NULL) {
      return NULL;
   }
   int bytes_needed = snprintf(bufp,
                               bufl,
                               "%s=%d,%d,%d,%d",
                               KM_EXEC_PIDINFO,
                               km_trace_include_pid_value(),
                               machine.ppid,
                               machine.pid,
                               machine.next_pid);
   if (bytes_needed + 1 > bufl) {
      free(bufp);
      return NULL;
   }
   return bufp;
}

/*
 * Called before exec.
 * Append km exec related state to the passed environment and return
 * a pointer to the combined environment.
 */
char** km_exec_build_env(char** envp)
{
   // Count the passed env
   int envc;
   for (envc = 0; envp[envc] != NULL; envc++) {
   }
   envc++;   // include the null stop pointer

   // Allocate a new env array with space for the exec related vars.
   char** newenvp = malloc((envc + KM_EXEC_VARS) * sizeof(char*));
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
   }

   // env var builder function addresses
   char* (*envvar_build[KM_EXEC_VARS])(void) = {km_exec_vers_var,
                                                km_exec_g2h_var,
                                                km_exec_vmfd_var,
                                                km_exec_eventfd_var,
                                                km_exec_pidinfo_var};

   // Add exec vars to the new env
   int j;
   char* envvarp;
   for (j = 0; j < KM_EXEC_VARS; j++) {
      if ((envvarp = envvar_build[j]()) == NULL) {
         free(newenvp);
         return NULL;
      }
      newenvp[i + j] = envvarp;
   }
   newenvp[i + j] = NULL;
   assert(i + j == envc - 1 + KM_EXEC_VARS);

   // Return pointer to the new env.
   return newenvp;
}

/*
 * Walk through the payload's environment vars to find the requested variable.
 */
static char* km_payload_getenv(char* varname)
{
   size_t vnl = strlen(varname);
   char** varp;
   km_infox(KM_TRACE_ARGS, "km_exec_payload_env %p, varname %s", km_exec_payload_env, varname);
   for (varp = km_exec_payload_env; *varp != NULL; varp++) {
      char* var_kma = km_gva_to_kma((km_gva_t)*varp);
      km_infox(KM_TRACE_ARGS, "var %p, var_kma %p, %s", *varp, var_kma, var_kma != NULL ? var_kma : "bad address");
      assert(var_kma != NULL);
      if (strncmp(var_kma, varname, vnl) == 0 && *(var_kma + vnl) == '=') {
         return var_kma + vnl + 1;
      }
   }
   return NULL;
}

/*
 * Resolve the passed command name to a km payload path which can be provided to km for exec.
 * Payloads specified with a relative path are resolve using the guest's PATH env var.
 */
static char* km_resolve_cmdname(char* cmdname, char* returnedpath, int returnedpathl, char**shebangarg)
{
   char* p = NULL;

   // Get an absolute path or a path relative to the current working dir.
   if (*cmdname == '/' || strncmp(cmdname, "./", 2) == 0) {  // path is specified
      snprintf(returnedpath, returnedpathl, "%s", cmdname);
      p = km_get_payload_name(returnedpath, shebangarg);
   } else {
      char* path = km_payload_getenv("PATH");
      if (path == NULL) {  // relative path and no payload PATH variable, can't help you
         km_infox(KM_TRACE_EXEC, "Couldn't get payload PATH env var");
         return NULL;
      }
      km_infox(KM_TRACE_EXEC, "Using PATH=%s", path);
      // Parse the value of the PATH var and try to find the path of the payload
      char temp[MAXPATHLEN];
      char* trailingcolon;
      char* pvp;
      for(pvp = path; pvp != NULL; pvp = trailingcolon) {
         if (*pvp == ':') {  // sometimes there is no leading ':'
            pvp++;
         }
         trailingcolon = strchr(pvp, ':');
         size_t l;
         if (trailingcolon == NULL) {  // no trailing colon
            l = strlen(pvp);
         } else {
            l = trailingcolon - pvp;
         }
         if (l < sizeof(path)) {
            strncpy(temp, pvp, l);
            temp[l] = 0;
            snprintf(returnedpath, returnedpathl, "%s/%s", temp, cmdname);
            // Handle payloads that are shebang files or the shebang resolves to a symlink
            p = km_get_payload_name(returnedpath, shebangarg);
            if (p != NULL) {
               break;
           }
         } else {
            km_infox(KM_TRACE_EXEC, "ignoring path %s: too long", pvp);
         }
      }
   }
   km_infox(KM_TRACE_EXEC, "Resolved cmd %s to path %s", cmdname, p);
   if (p == NULL) {
      return NULL;
   }
   if (strlen(p) >= returnedpathl) {
      km_info(KM_TRACE_EXEC, "Not enough space to return path %s", p);
      free(p);
      return NULL;
   }
   strcpy(returnedpath, p);
   free(p);
   
   return returnedpath;
}

/*
 * Build an argv[] by taking the current km's args other than the payload args and
 * appending the argv passed to execve().
 * If they are trying to run the shell, we don't want to do that for security reasons.
 * Instead we parse the command line that would be passed to the shell and form a new
 * argv[] which will be appended to the km command line as would be done if a km payload
 * was being exec'ed.
 */
static char** km_parse_shell_cmd_line(char* cmdline);
static void freevector(char** sv);
char** km_exec_build_argv(char* filename, char** argv)
{
   char** nargv;
   int nargc;
   int argc;
   char** newargv = NULL;
   char* shebong;

   if (strcmp(filename, SHELL_PATH) == 0) {   // so that popen() calls from the payload can work
      assert(argv[0] != NULL && argv[1] != NULL && argv[2] != NULL);
      assert(strcmp(km_gva_to_kma((km_gva_t)argv[1]), "-c") == 0);
      char* cmdline = km_gva_to_kma((km_gva_t)argv[2]);
      km_infox(KM_TRACE_EXEC, "Parsing shell command: %s", cmdline);
      newargv = km_parse_shell_cmd_line(cmdline);
      if (newargv == NULL) {
         return NULL;
      }
      // Resolve newargv[0] to a path to a km payload
      char resolvedpath[MAXPATHLEN * 2];     // * 2 for compiler pacification :-(
      filename = km_resolve_cmdname(newargv[0], resolvedpath, sizeof(resolvedpath), &shebong);
      if (filename == NULL) {
         freevector(newargv);
         return NULL;
      }

      for (argc = 0; newargv[argc] != NULL; argc++) {
      }
      if (shebong != NULL) {
         argc++;
      }
   } else {   // a km payload file
      for (argc = 0; argv[argc] != NULL; argc++) {
      }
   }

   // Allocate memory for the km args plus the payload args
   nargc = 1 + argc + 1;
   if ((nargv = calloc(nargc, sizeof(char*))) == NULL) {
      freevector(newargv);
      return NULL;
   }

   // Copy km related args in.  Still deciding what else to include
   int i = 0;
   nargv[i++] = km_get_self_name();

   // path to the km payload being "exec"ed
   nargv[i] = filename;
   if (shebong != NULL) {
      nargv[++i] = shebong;
   }

   // Copy passed args in
   int j;
   for (j = 1; j < argc; j++) {
      nargv[i + j] = newargv != NULL ? newargv[j] : km_gva_to_kma((km_gva_t)argv[j]);
      if (nargv[i + j] == NULL) {
         free(nargv);
         freevector(newargv);
         return NULL;
      }
   }
   nargv[i + j] = NULL;

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

static int km_exec_get_guestfds(char* guestfds)
{
   int n;
   char* pi;
   char* p;
   char* saveptr;

   // Get the guest to host fd map, note: strtok clobbers the input string
   char* guestfds_copy = strdup(guestfds);
   if (guestfds_copy == NULL) {
      return -1;
   }
   pi = guestfds_copy;
   int i = 0;
   while ((p = strtok_r(pi, ",", &saveptr)) != NULL) {
      pi = NULL;
      if (i >= execstatep->nfdmap) {   // too many fd's?
         return -1;
      }
      n = sscanf(p, "%d:%d", &execstatep->guestfd_hostfd[i].guestfd, &execstatep->guestfd_hostfd[i].hostfd);
      if (n != 2) {
         return -1;
      }
      i++;
   }
   free(guestfds_copy);
   if (i < execstatep->nfdmap) {   // if there is room, add a terminator
      execstatep->guestfd_hostfd[i].guestfd = -1;
   }
   return 0;
}

// Called before running the execed program.  Picks up what was saved by km_exec_save_kmstate()
int km_exec_recover_kmstate(void)
{
   char* vernum = getenv(KM_EXEC_VERS);
   char* vmfds = getenv(KM_EXEC_VMFDS);
   char* eventfds = getenv(KM_EXEC_EVENTFDS);
   char* guestfds = getenv(KM_EXEC_GUESTFDS);
   char* pidinfo = getenv(KM_EXEC_PIDINFO);
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
   n = sscanf(vernum, "%d,%d", &version, &nfdmap);
   if (n != 2) {
      km_infox(KM_TRACE_EXEC, "couldn't process vernum %s, n %d", vernum, n);
      return -1;
   }
   if (version != KM_EXEC_VERNUM) {
      // Hmmm, did they install a new version of km while we were running?
      km_infox(KM_TRACE_EXEC, "exec state verion mismatch, got %d, expect %d", version, KM_EXEC_VERNUM);
      return -1;
   }
   execstatep = calloc(1, sizeof(*execstatep) + (sizeof(fdmap_t) * nfdmap));
   if (execstatep == NULL) {
      km_infox(KM_TRACE_EXEC, "couldn't allocate fd map, nfdmap %d", nfdmap);
      return -1;
   }
   execstatep->version = version;
   execstatep->nfdmap = nfdmap;

   if (km_exec_get_vmfds(vmfds) != 0) {
      return -1;
   }

   // Get the event fd's
   n = sscanf(eventfds, "%d,%d", &execstatep->intr_fd, &execstatep->shutdown_fd);
   if (n != 2) {
      km_infox(KM_TRACE_EXEC, "couldn't get event fd's %s, n %d", eventfds, n);
      return -1;
   }

   if (km_exec_get_guestfds(guestfds) != 0) {
      km_infox(KM_TRACE_EXEC, "couldn't parse guestfds %s", guestfds);
      return -1;
   }

   // Get the pid information
   n = sscanf(pidinfo,
              "%d,%d,%d,%d",
              &execstatep->tracepid,
              &execstatep->ppid,
              &execstatep->pid,
              &execstatep->next_pid);
   if (n != 4) {
      km_infox(KM_TRACE_EXEC, "couldn't scan pidinfo %s, n %d", pidinfo, n);
      return -1;
   }

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
   char linkname[32];
   char linkbuf[MAXPATHLEN];

   if (execstatep == NULL) {
      return 1;
   }
   for (int i = 0; i < execstatep->nfdmap && execstatep->guestfd_hostfd[i].guestfd >= 0; i++) {
      ssize_t bytes;
      snprintf(linkname, sizeof(linkname), PROC_SELF_FD, execstatep->guestfd_hostfd[i].hostfd);
      if ((bytes = readlink(linkname, linkbuf, sizeof(linkbuf) - 1)) < 0) {
         err(2, "Can't get filename for hostfd %d, link %s", execstatep->guestfd_hostfd[i].hostfd, linkname);
      }
      linkbuf[bytes] = 0;
      km_infox(KM_TRACE_EXEC,
               "guestfd %d, hostfd %d, name %s",
               execstatep->guestfd_hostfd[i].guestfd,
               execstatep->guestfd_hostfd[i].hostfd,
               linkbuf);
      int chosen_guestfd = km_add_guest_fd(NULL,
                                           execstatep->guestfd_hostfd[i].hostfd,
                                           execstatep->guestfd_hostfd[i].guestfd,
                                           linkbuf,
                                           0);
      assert(chosen_guestfd == execstatep->guestfd_hostfd[i].guestfd);
   }
   return 0;
}

/*
 * Called in the exec'ed program before setting up the vm to get rid of open file descriptors from
 * exec. If we mark these fd's as close on exec, we wouldn't need to do this.
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

void km_exec_init(int argc, char** argv)
{
   // Remember this in case the guest performs an execve().
   km_exec_argc = argc;   // km-specific argc ... all args after that belong to payload
   km_exec_argv = argv;

   switch (km_exec_recover_kmstate()) {
      case 0:   // this invocation is not a result of a payload execve()
         break;
      case 1:   // payload did an exec, close open fd's
         km_exec_vmclean();
         machine.ppid = execstatep->ppid;
         machine.pid = execstatep->pid;
         machine.next_pid = execstatep->next_pid;
         km_trace_include_pid(execstatep->tracepid);
         break;
      default:   // exec state is messed up
         errx(2, "Problems in performing post exec processing");
         break;
   }
}

void km_exec_fini(void)
{
   free(execstatep);
   execstatep = NULL;
}

/*
 * Get a quoted string.  The first char is the quote char.
 * Allow escaped chars in the string.
 * Return a pointer to allocated memory containing the string without the quoting chars
 * at the beginning and end of the string.
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
         } else {   // backslash followed by string terminator?
            return NULL;
         }
      } else if (*e != quote) {
         e++;
      } else {  // terminating quote
         char* a = malloc(e - s);
         if (a == NULL) {  // no memory
            return NULL;
         }
         char* d = a;
         char* t = s + 1;
         while (*t != 0) {
            if (*t == '\\') {  // skip escaped char marker
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
   // No terminating quote?
   return NULL;
}

static void freevector(char** sv)
{
   km_infox(KM_TRACE_EXEC, "freeing arg vector %p", sv);
   if (sv != NULL) {
      for (int i = 0; sv[i] != NULL; i++) {
         km_infox(KM_TRACE_EXEC, "freeing %p", sv[i]);
         free(sv[i]);
      }
      free(sv);
   }
}

/*
 * Parse a shell command line and return the arguments in an allocated
 * address vector.  The caller is responsible for freeing the address vector
 * and the memory pointed to by the vector elements.
 * This function will fail if it finds any special shell constructs, like:
 *   i/o redirection
 *   backgrounding
 *   pipelining
 *   shell variables
 *   regular expressions
 *   command evaluation like `echo abc`
 *   compound commands (&& ||)
 * Returns:
 *   vector address on success
 *   NULL on failure
 */
static char** km_parse_shell_cmd_line(char* cmdline)
{
   char* s = cmdline;
   int svi = 0;
   char** sv = NULL;
   char** newsv;

   km_infox(KM_TRACE_EXEC, "parsing cmdline %s", cmdline);

   for (;;) {
      // skip leading whistspace
      while (*s != 0 && (*s == ' ' || *s == '\t')) {
         s++;
     }
     if (*s == 0) { // nothing left
        break;
     }

     char* a;
     if (*s == '"' || *s =='\'') { // Handle quoted string
        a = get_quoted_string(&s);
     } else {  // whitespace delimited string
        int l;
        char* e = strpbrk(s, " \t");
        if (e == NULL) {
           l = strlen(s);
        } else {
           l = e - s;
        }
        a = malloc(l + 1);
        if (a == NULL) {
           km_infox(KM_TRACE_EXEC, "Couldn't allocate %d bytes", l + 1);
           freevector(sv);
           return NULL;
        }
        strncpy(a, s, l);
        a[l] = 0;
        s += l;
      }
      km_infox(KM_TRACE_EXEC, "arg[%d] = %s", svi, a);
      newsv = realloc(sv, (svi + 2) * sizeof(char*));
      if (newsv == NULL) {
         km_infox(KM_TRACE_EXEC, "realloc() failed, needed %lu bytes", (svi + 1) * sizeof(char*));
         freevector(sv);
         return NULL;
      }
      sv = newsv;
      sv[svi++] = a;
      sv[svi] = NULL;
   }

   km_infox(KM_TRACE_EXEC, "returning sv at %p with %d elements", sv, svi);
   return sv;
}

