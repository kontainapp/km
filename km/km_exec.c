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

#include "km_exec.h"
#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
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
 */
#define KM_EXEC_VARS 4
static const int KM_EXEC_VERNUM = 1;
static char KM_EXEC_VERS[] = "KM_EXEC_VERS";
static char KM_EXEC_VMFDS[] = "KM_EXEC_VMFDS";
static char KM_EXEC_EVENTFDS[] = "KM_EXEC_EVENTFDS";
static char KM_EXEC_GUESTFDS[] = "KM_EXEC_GUESTFDS";

/*
 * An instance of this holds all of the exec state that the execing program puts
 * into the above environment variables.  The exec'ed instance of km retreives the
 * environment variables and builds an instance of this structure which is then
 * used to restore needed km state for the new payload to continue.
 */
typedef struct fdmap {
   int guestfd;
   int hostfd;
} fdmap_t;

typedef struct km_exec_state {
   int version;
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
   char* (*envvar_build[KM_EXEC_VARS])(
       void) = {km_exec_vers_var, km_exec_g2h_var, km_exec_vmfd_var, km_exec_eventfd_var};

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
 * Build an argv[] by taking the current km's args other than the payload args and
 * appending the argv passed to execve().
 */
char** km_exec_build_argv(char* filename, char** argv)
{
   char** nargv;
   int nargc;

   // Count km related args from the invoking program.
   int tmpargc;
   for (tmpargc = 1; tmpargc < km_exec_argc && km_exec_argv[tmpargc][0] == '-'; tmpargc++) {
   }
   int argc;
   for (argc = 0; argv[argc] != NULL; argc++) {
   }

   // Allocate memory for the km args plus the passed args
   nargc = tmpargc + argc + 1;
   nargv = calloc(nargc, sizeof(char*));
   if (nargv == NULL) {
      return NULL;
   }

   // Copy km related args in
   int i;
   for (i = 0; i < tmpargc; i++) {
      nargv[i] = km_exec_argv[i];
   }

   // Copy passed args in
   nargv[i] = filename;
   int j;
   for (j = 1; j < argc; j++) {
      nargv[i + j] = km_gva_to_kma((km_gva_t)argv[j]);
      if (nargv[i + j] == NULL) {
         free(nargv);
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
   int version;
   int nfdmap;
   int n;

   if (vernum == NULL || vmfds == NULL || eventfds == NULL || guestfds == NULL) {
      // If we don't have them all, then this isn't an exec().
      // And, if one is missing they all need to be missing.
      assert(vernum == NULL && vmfds == NULL && eventfds == NULL && guestfds == NULL);
      return 0;
   }
   n = sscanf(vernum, "%d,%d", &version, &nfdmap);
   if (n != 2) {
      return -1;
   }
   if (version != KM_EXEC_VERNUM) {
      // Hmmm, did they install a new version of km while we were running?
      return -1;
   }
   execstatep = calloc(1, sizeof(*execstatep) + (sizeof(fdmap_t) * nfdmap));
   if (execstatep == NULL) {
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
      return -1;
   }

   if (km_exec_get_guestfds(guestfds) != 0) {
      return -1;
   }

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
      snprintf(linkname, sizeof(linkname), "/proc/self/fd/%d", execstatep->guestfd_hostfd[i].hostfd);
      if ((bytes = readlink(linkname, linkbuf, sizeof(linkbuf))) < 0) {
         err(2, "Can't get filename for hostfd %d, link %s", execstatep->guestfd_hostfd[i].hostfd, linkname);
      }
      linkname[bytes] = 0;
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
   km_exec_argc = argc;
   km_exec_argv = argv;

   switch (km_exec_recover_kmstate()) {
      case 0:   // this invocation is not a result of a payload execve()
         break;
      case 1:   // payload did an exec, close open fd's
         km_exec_vmclean();
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
