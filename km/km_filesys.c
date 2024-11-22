/*
 * Copyright 2022-2023 Kontain Inc
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

/*
 * Notes on File Descriptors
 * -------------------------
 *
 * There is no explicit mapping function to translate guest file descriptor to
 * host file descriptor. To accomplish this, the guest is presented with a smaller
 * RLIMIT_FSIZE then the KM process sees and high fd numbers are used for KM internal
 * file descriptors (for example KVM/KKM control fd's).
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <linux/aio_abi.h>
#include <netinet/in.h>

#include "bsd_queue.h"
#include "km.h"
#include "km_coredump.h"
#include "km_exec.h"
#include "km_filesys.h"
#include "km_filesys_private.h"
#include "km_iocontext.h"
#include "km_mem.h"
#include "km_signal.h"
#include "km_snapshot.h"
#include "km_syscall.h"

/*
 * km uses open fd's for its own purposes.  The km fd's are kept at the
 * top of the fd space.  These are the fd's that km uses:
 *  kvm/kkm virt dev fd
 *  per vcpu fd (up to 288)
 *  shutdown fd
 *  intr fd
 *  gdb listen fd
 *  gdb active connection fd
 *  km mgmt listen fd
 *  km mgmt connection fd
 *  snapshot input fd
 *  snapshot output fd
 *  km logging/tracing fd
 *
 * In addition the snapshot recover fd overloads the gdb active connection fd since
 * open snapshots are not used concurrent with gdb accessing the payload.
 * If concurrency is required we will need to allocate another km fd for an open
 * snapshot file.
 */
const int MAX_OPEN_FILES = 1024;
const int MAX_KM_FILES = KVM_MAX_VCPUS + 2 + 2 + 2 + 2 + 1;   // eventfds, kvm, gdb, km mgmt, snap, log
const int KM_GDB_LISTEN = MAX_OPEN_FILES - MAX_KM_FILES;
const int KM_GDB_ACCEPT = MAX_OPEN_FILES - MAX_KM_FILES + 1;
const int KM_MGM_LISTEN = MAX_OPEN_FILES - MAX_KM_FILES + 2;
const int KM_MGM_ACCEPT = MAX_OPEN_FILES - MAX_KM_FILES + 3;
const int KM_LOGGING = MAX_OPEN_FILES - MAX_KM_FILES + 4;
const int KM_START_FDS = MAX_OPEN_FILES - MAX_KM_FILES + 5;

static char proc_pid_fd[128];
static char proc_pid_exe[128];
static char proc_pid[128];
static int proc_pid_length;

static char* km_my_exec;   // my executable per /proc/self/exe to check with in readlink

// file name conversion functions forward declarations
static int km_fs_g2h_filename(const char* name, char* buf, size_t bufsz, km_file_ops_t** ops);
static int km_fs_g2h_readlink(const char* name, char* buf, size_t bufsz);
static void km_snapshot_listenfds_find(km_elf_t* e);
static void km_snapshot_listenfds_free(void);

km_fd_dup_data_t dup_data;
pthread_mutex_t dup_data_mtx;

/*
 * Lightweight snap start - wait for connection on the snap_listen_sock before restoring the
 * snapshot. The env var KM_SNAP_LISTEN_TIMEOUT supplies this value.
 */
int64_t light_snap_accept_timeout;   // != 0 means light weight accept is enabled
                                     // < 0 means shrink only by accept count
                                     // > 0 means shrink by accept count and timeout

/*
 * Use this to keep track of which fd's are listening inside of a snapshot.
 * We fill this from the open file notes in the snapshot elf file.
 */
typedef struct km_snap_listening_state {
   int listen_fd;
   int accept_fd;   // if >= 0, this listen fd rehydrated the payload
   km_fd_socket_t listen_sockinfo;
} km_snap_listening_state_t;
km_snap_listening_state_t* km_snap_listening_state_p;
int km_snap_listening_state_max;   // number of array in what km_snap_listening_state_p points to
int km_snap_listening_state_cnt;   // how many elements in km_snap_listening_state_p[] are in use
int km_shrunken = 0;               // set to non-zero when payload shrinks via exec to km.

static u_int64_t accept_time;   // time stamp of the latest accept HC in milliseconds
static int64_t accept_cnt;      // count of active accepted sockets

// Account for new accept: ++ count and record the time
static inline void km_set_active_accept(void)
{
   if (light_snap_accept_timeout != 0) {
      __atomic_add_fetch(&accept_cnt, 1, __ATOMIC_SEQ_CST);
      if (light_snap_accept_timeout > 0) {
         struct timespec tp;
         if (clock_gettime(CLOCK_MONOTONIC, &tp) != 0) {
            km_err(2, "can't update accept time");
         }
         accept_time = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
      }
   }
}

// Account for closing accepted socket: -- count
static inline void km_set_inactive_accept(void)
{
   if (light_snap_accept_timeout != 0) {
      int64_t cnt = __atomic_sub_fetch(&accept_cnt, 1, __ATOMIC_SEQ_CST);
      km_assert(accept_cnt >= 0);
      if (light_snap_accept_timeout < 0 && cnt == 0) {   // this is the last one
         km_signal_machine_fini();
      }
   }
}

/*
 * Check for active accepted sockets, and if none - check for how long since the last accept.
 * Return 1 if there *are* active accepted sockets, and 0 if none for the timeout
 */
int km_active_accept(void)
{
   if (__atomic_load_n(&accept_cnt, __ATOMIC_SEQ_CST) != 0) {   // there are active accepted sockets
      return 1;
   }
   if (light_snap_accept_timeout < 0) {   // go by count only
      return 0;
   }
   struct timespec tp;
   if (clock_gettime(CLOCK_MONOTONIC, &tp) != 0) {
      km_err(2, "can't get time");
   }
   return tp.tv_sec * 1000 + tp.tv_nsec / 1000000 > accept_time + light_snap_accept_timeout ? 0 : 1;
}

static int km_snap_is_listener(int fd)
{
   for (int i = 0; i < km_snap_listening_state_cnt; i++) {
      if (km_snap_listening_state_p[i].listen_fd == fd) {
         return 1;
      }
   }
   return 0;
}

static void km_vmstate_destroy(int elf_fd)
{
   int i;
   struct stat statb;

   for (i = 3; i < MAX_OPEN_FILES; i++) {
      if (km_snap_is_listener(i) == 0 && i != KM_LOGGING && i != elf_fd && fstat(i, &statb) == 0) {
         close(i);
      }
   }
}

/*
 * If the passed listening fd has an accepted connection that
 * was accepted before the payload was hydrated, then return that
 * fd.
 * If we find nothing return -1
 */
static int km_snap_find_accepted(int listenfd)
{
   for (int i = 0; i < km_snap_listening_state_cnt; i++) {
      if (km_snap_listening_state_p[i].listen_fd == listenfd) {
         int accept_fd;
         if ((accept_fd = km_snap_listening_state_p[i].accept_fd) >= 0) {
            km_snap_listening_state_p[i].accept_fd = -1;
            km_snapshot_listenfds_free();
            return accept_fd;
         }
         break;
      }
   }
   return -1;
}

// helper routine to get network port numbers for messages
static uint16_t extract_port(struct sockaddr* sap)
{
   switch (sap->sa_family) {
      case AF_INET:
         return ntohs(((struct sockaddr_in*)sap)->sin_port);
      case AF_INET6:
         return ntohs(((struct sockaddr_in6*)sap)->sin6_port);
      default:
         km_errx(2, "Unsupported sockaddr address family %d", sap->sa_family);
   }
   return 0;
}

void light_snap_listen(km_elf_t* e)
{
   /*
    * SNAP_LISTEN_TIMEOUT's presence in the environment enables the light
    * snap listen mode of operation.
    */
   char* to = getenv(KM_SNAP_LISTEN_TIMEOUT);
   if (to != NULL) {
      light_snap_accept_timeout = atol(to);
   }
   if (light_snap_accept_timeout == 0) {
      return;
   }

   // Build a list of the listening socket fd's in the snapshot
   km_snapshot_listenfds_find(e);
   km_infox(KM_TRACE_SHRINK,
            "light_snap_accept_timeout %ld ms, listener count %d",
            light_snap_accept_timeout,
            km_snap_listening_state_cnt);
   if (km_snap_listening_state_cnt <= 0) {
      // No listening fd's in the snapshot.
      // We have nothing to trigger shrinking an idle running snapshot so
      // just run as normal.
      km_infox(KM_TRACE_SNAPSHOT, "snapshot has no listening sockets");
      return;
   }

   // If we got here because the payload shrank itself, the listening
   // socket fd's will be open so there is no need to recreate them.
   struct stat statb;
   if (fstat(km_snap_listening_state_p[0].listen_fd, &statb) == 0) {
      km_assert((statb.st_mode & S_IFMT) == S_IFSOCK);
      // We've been exec'ed to by a payload shrink, the payload's fd's are still open.
      // Get rid of vm related fd's and payload fd's we inherited from the previous instance of
      // km But, we keep the payload's listening fd's and the open snapshot.
      km_vmstate_destroy(fileno(e->file));
      km_infox(KM_TRACE_SNAPSHOT,
               "Using %d listening fd's that existed before shrinking",
               km_snap_listening_state_cnt);
      km_shrunken = 1;
   }

   // Create open fd's for all the listening sockets.  If we got here because
   // the running snapshot shrank itself, then the listening socket fd's will
   // be open so we don't need to open anything.
   // It is important that earlier parts of km do NOT use the payload fd space.
   // That fd may be one of the fd's that the resuming snapshot will be listening
   // on and the existence of the open fd will confuse this into thinking we are
   // here because of a shrink operation.
   for (int i = 0; km_shrunken == 0 && i < km_snap_listening_state_cnt; i++) {
      km_infox(KM_TRACE_SHRINK,
               "%d: listen_fd %d, accept_fd %d, port %d",
               i,
               km_snap_listening_state_p[i].listen_fd,
               km_snap_listening_state_p[i].accept_fd,
               ntohs(((struct sockaddr_in*)km_snap_listening_state_p[i].listen_sockinfo.addr)->sin_port));
      km_assert(fstat(km_snap_listening_state_p[i].listen_fd, &statb) < 0);
      km_snap_listening_state_t* p = &km_snap_listening_state_p[i];
      int fd = socket(p->listen_sockinfo.domain, p->listen_sockinfo.type, p->listen_sockinfo.protocol);
      if (fd < 0) {
         km_err(2, "socket() can't create listening fd %d", p->listen_fd);
      }
      km_warnx("creating listening socket: fd %d, listen_fd %d, port %d",
               fd,
               p->listen_fd,
               extract_port((struct sockaddr*)p->listen_sockinfo.addr));
      // We force SO_REUSEADDR even if the snapshoted program didn't use it.
      int flag = 1;
      if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0) {
         km_err(2, "setsockopt(SO_REUSEADDR) failed");
      }
      if (bind(fd, (struct sockaddr*)p->listen_sockinfo.addr, p->listen_sockinfo.addrlen) < 0) {
         km_err(2,
                "bind() fd %d to port %d failed",
                p->listen_fd,
                extract_port((struct sockaddr*)p->listen_sockinfo.addr));
      }
      if (listen(fd, p->listen_sockinfo.backlog) < 0) {
         km_err(2, "listen failed, fd %d", p->listen_fd);
      }
      if (fd != p->listen_fd) {
         if (dup2(fd, p->listen_fd) < 0) {
            km_err(2, "listen failed, fd %d", p->listen_fd);
         }
         close(fd);
      }
   }

   int i;
   int snap_conn_sock = -1;
   do {
      int maxfd = 0;
      fd_set fds;

      // Build the mask of the listening fd's to wait for.
      FD_ZERO(&fds);
      for (i = 0; i < km_snap_listening_state_cnt; i++) {
         FD_SET(km_snap_listening_state_p[i].listen_fd, &fds);
         if (km_snap_listening_state_p[i].listen_fd > maxfd) {
            maxfd = km_snap_listening_state_p[i].listen_fd;
         }
      }

      // Wait for something to happen on a listening fd.
      int rc;
      do {
         rc = select(maxfd + 1, &fds, NULL, NULL, NULL);
         if (rc < 0 && errno != EINTR) {
            km_err(2, "select in light accept");
         }
      } while (rc < 0 && errno == EINTR);

      // Find the fd that fired
      int trigered_fd = -1;
      for (i = 0; i < km_snap_listening_state_cnt; i++) {
         if (FD_ISSET(km_snap_listening_state_p[i].listen_fd, &fds)) {
            trigered_fd = km_snap_listening_state_p[i].listen_fd;
            break;
         }
      }
      km_assert(trigered_fd >= 0);
      if ((snap_conn_sock = accept(trigered_fd, NULL, NULL)) < 0) {
         km_err(2, "accept failed");
      }

      // peek in http header for knative readiness probe
      char buf[1024];
      char wbuf[] = "HTTP/1.1 200 OK\n"
                    "Content-Length: 0\n"
                    "Content-Type: text/html\n"
                    "Connection: Closed\n"
                    "\n";
      rc = recv(snap_conn_sock, buf, sizeof(buf), MSG_PEEK);
      if (rc < 0) {
         km_err(2, "can't peek in accepted socket data");
      }
      buf[rc] = '\0';
      km_infox(KM_TRACE_SNAPSHOT, "connection: ---\n%s\n---", buf);
      if (strstr(buf, "User-Agent: kube-probe") != NULL) {
         write(snap_conn_sock, wbuf, sizeof(wbuf));
         shutdown(snap_conn_sock, SHUT_RDWR); /* no more receptions */
         close(snap_conn_sock);
         snap_conn_sock = -1;
      }
   } while (snap_conn_sock < 0);

   // reuse mgmt socket fd number here, resumed snapshots currently don't have
   // a mgmmt thread.
   km_snap_listening_state_p[i].accept_fd = km_internal_fd(snap_conn_sock, KM_MGM_ACCEPT);
}

/*
 * Called to have a running snapshot reduced back to km listening for a connection
 * which when accept will cause the snapshot payload to be started up again.
 * If successful this function will not return.
 */
int km_shrink_footprint(km_vcpu_t* vcpu)
{
   char* envarray[5];
   char* argv[3];
   char timeout[32];
   char kmverbose[32];
   char me[128];
   char* tmp;

   /*
    * If we aren't running a snapshot or the SNAP_LISTEN_TIMEOUT
    * env var is undefined, then km will not stay in shrunken state
    * after exec'ing back to km, so there is no reason to attempt
    * a shrink payload operation.
    */
   km_infox(KM_TRACE_SNAPSHOT,
            "light_snap_accept_timeout %d, km_snapshot_name %s",
            light_snap_accept_timeout,
            km_snapshot_name);
   if (km_snapshot_name == NULL || light_snap_accept_timeout == 0) {
      km_warnx("not running in a snapshot");
      return EINVAL;
   }
   km_vcpu_pause_all(vcpu, ALL);
   // recheck active accepted sockets to make sure none snuck in after the lighter check
   int rc = 0;
   if (__atomic_load_n(&accept_cnt, __ATOMIC_SEQ_CST) == 0) {
      int i = 0;
      if ((tmp = getenv(KM_VERBOSE)) != NULL) {
         snprintf(kmverbose, sizeof(kmverbose), "%s=%s", KM_VERBOSE, tmp);
         envarray[i++] = kmverbose;
      }
      if ((tmp = getenv(KM_SNAP_LISTEN_TIMEOUT)) != NULL) {
         snprintf(timeout, sizeof(timeout), "%s=%s", KM_SNAP_LISTEN_TIMEOUT, tmp);
         envarray[i++] = timeout;
      }
      envarray[i] = NULL;
      ssize_t meleng = readlink(PROC_SELF_EXE, me, sizeof(me) - 1);
      if (meleng < 0) {
         km_warn("readlink( %s ) failed", PROC_SELF_EXE);
         return errno;
      }
      me[meleng] = 0;
      argv[0] = me;
      argv[1] = km_snapshot_name;
      argv[2] = NULL;
      km_infox(KM_TRACE_SNAPSHOT,
               "execve() to %s, argv[1] %s, envarrary[0] %s, [1] %s, [2] %s",
               me,
               argv[1],
               envarray[0],
               envarray[1],
               envarray[2]);
      rc = execve(me, argv, envarray);
      // We got here, something went wrong.
      rc = errno;
      km_warn("execve() to %s failed", me);
   } else {
      km_warnx("aborted shrink");
   }

   km_vcpu_resume_all();
   return rc;
}

/*
 * Tells whether a file is inuse or not.
 */
int km_is_file_used(km_file_t* file)
{
   return (__atomic_load_n(&file->inuse, __ATOMIC_SEQ_CST) != 0);
}

void km_set_file_used(km_file_t* file, int val)
{
   __atomic_store_n(&file->inuse, val, __ATOMIC_SEQ_CST);
}

/*
 * On snap recover check if the fd was involved in dup. If it was check if other fds in the same dup
 * group exist already. If they are dup them, if not we are fist so recreate normally.
 *
 * This is called during snapshot recovery, single threaded, no lock necessary
 */
static int km_fs_check_for_dups_nolock(int fd)
{
   for (int grp = 0; grp < dup_data.size; grp++) {
      km_fd_dup_grp_t* group = dup_data.groups[grp];
      int i;
      for (i = 0; i < group->size; i++) {
         if (fd == group->fds[i]) {
            break;
         }
      }
      if (i == group->size) {   // we are not in this group
         continue;
      }
      for (int i = 0; i < group->size; i++) {
         int ofd = group->fds[i];
         // not us and already exists - we are dup of it
         if (ofd != fd && km_is_file_used(&km_fs()->guest_files[ofd])) {
            return ofd;
         }
      }
      return -2;   // found ourselves in the group but others aren't ready - we are the first
   }
   return -1;   // not dup
}

static void km_fs_add_fd_to_dup_group(int fd, km_fd_dup_grp_t* group)
{
   if ((group->fds = realloc(group->fds, (group->size + 1) * sizeof(group->fds[0]))) == NULL) {
      km_err(2, "no memory for dup group");
   }
   group->fds[group->size++] = fd;
}

/*
 * Called during normal run to record dup operation
 */
static void km_fs_add_to_dup_data(int new_fd, int old_fd)
{
   if (machine.mmaps.recovery_mode != 0) {
      // during snapshot recovery dup data gets restored fist, then used to restore files
      return;
   }
   km_mutex_lock(&dup_data_mtx);
   for (int grp = 0; grp < dup_data.size; grp++) {
      km_fd_dup_grp_t* group = dup_data.groups[grp];
      for (int i = 0; i < group->size; i++) {
         int fd = group->fds[i];
         if (fd == old_fd) {
            // old_fd already in a group, add ourselves to it
            km_fs_add_fd_to_dup_group(new_fd, group);
            km_mutex_unlock(&dup_data_mtx);
            return;
         }
      }
   }
   // new group
   km_fd_dup_grp_t* group = malloc(sizeof(km_fd_dup_grp_t));
   if (group == NULL) {
      km_err(2, "no memory for dup group");
   }
   group->size = 2;
   if ((group->fds = malloc(2 * sizeof(group->fds[0]))) == NULL) {
      km_err(2, "no memory for dup group fds");
   }
   group->fds[0] = old_fd;
   group->fds[1] = new_fd;
   if ((dup_data.groups =
            realloc(dup_data.groups, (dup_data.size + 1) * sizeof(dup_data.groups[0]))) == NULL) {
      km_err(2, "no memory for dup data groups");
   }
   dup_data.groups[dup_data.size++] = group;
   km_mutex_unlock(&dup_data_mtx);
}

/*
 * Called during normal run to record closes of duped fds
 */
static void km_fd_close_dup(int fd)
{
   km_mutex_lock(&dup_data_mtx);
   for (int grp = 0; grp < dup_data.size; grp++) {
      km_fd_dup_grp_t* group = dup_data.groups[grp];
      for (int i = 0; i < group->size; i++) {
         if (fd == group->fds[i]) {
            if (group->size > 2) {   // there are more than 2 dups - remove ourselves
               if (i < group->size - 1) {
                  memmove(&group->fds[i],
                          &group->fds[i + 1],
                          (group->size - 1 - i) * sizeof(group->fds[0]));
               }
               group->size--;
               group->fds = realloc(group->fds, group->size * sizeof(group->fds[0]));
            } else {   // only 2 dups - remove the group
               free(group->fds);
               free(group);
               if (grp < dup_data.size - 1) {
                  memmove(&dup_data.groups[grp],
                          &dup_data.groups[grp + 1],
                          (dup_data.size - 1 - grp) * sizeof(dup_data.groups[0]));
               }
               dup_data.size--;
               dup_data.groups = realloc(dup_data.groups, dup_data.size * sizeof(dup_data.groups[0]));
            }
            km_mutex_unlock(&dup_data_mtx);
            return;
         }
      }
   }
   km_mutex_unlock(&dup_data_mtx);
}

/*
 * Get file name for file descriptors that are not files - socket, pipe, and such
 */
char* km_get_nonfile_name(int hostfd)
{
   char buf[PATH_MAX + 1];
   char fn[128];
   snprintf(fn, sizeof(fn), PROC_SELF_FD, hostfd);

   int ret = readlink(fn, buf, PATH_MAX);
   if (ret < 0) {
      return NULL;
   }
   return strndup(buf, ret);
}

/*
 * Adds a host fd to the guest. Returns the guest fd number assigned.
 * Assigns lowest available guest fd, just like the kernel.
 * TODO: Support open flags (O_CLOEXEC in particular)
 */
static int km_add_guest_fd_internal(
    km_vcpu_t* vcpu, int host_fd, char* name, int flags, km_file_how_t how, km_file_ops_t* ops)
{
   km_assert(host_fd >= 0 && host_fd < km_fs()->nfdmap);
   int available = 0;
   int taken = 1;
   km_file_t* file = &km_fs()->guest_files[host_fd];
   if (__atomic_compare_exchange_n(&file->inuse, &available, taken, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ==
       0) {
      km_pathetic_stacktrace();
      km_abortx("open file slot %d already open on file %s, new file %s ", host_fd, file->name, name);
   }
   file->ops = ops;
   file->how = how;
   file->ofd = -1;
   file->sockinfo = NULL;
   TAILQ_INIT(&file->events);
   if (name == NULL) {
      file->name = km_get_nonfile_name(host_fd);
   } else {
      file->name = strdup(name);
   }
   file->flags = flags;
   return host_fd;
}

static int km_add_guest_fd(km_vcpu_t* vcpu, int host_fd, char* name, int flags, km_file_ops_t* ops)
{
   return km_add_guest_fd_internal(vcpu, host_fd, name, flags, KM_FILE_HOW_OPEN, ops);
}

static inline void km_connect_files(km_vcpu_t* vcpu, int guestfd[2])
{
   km_fs()->guest_files[guestfd[0]].ofd = guestfd[1];
   km_fs()->guest_files[guestfd[1]].ofd = guestfd[0];
}

static inline int km_add_socket_fd(
    km_vcpu_t* vcpu, int hostfd, char* name, int flags, int domain, int type, int protocol, km_file_how_t how)
{
   int ret = km_add_guest_fd_internal(vcpu, hostfd, name, flags, how, NULL);
   if (ret >= 0) {
      km_fd_socket_t* sockinfo = malloc(sizeof(km_fd_socket_t));
      *sockinfo = (km_fd_socket_t){.domain = domain, .type = type, .protocol = protocol};
      km_fs()->guest_files[ret].sockinfo = sockinfo;
   }
   return ret;
}

static inline int km_dup_socket_fd(
    km_vcpu_t* vcpu, int hostfd, char* name, int flags, km_fd_socket_t* sockinfo, km_file_how_t how)
{
   int ret = km_add_guest_fd_internal(vcpu, hostfd, name, flags, how, NULL);
   if (ret >= 0) {
      km_file_t* file = &km_fs()->guest_files[ret];
      km_assert(file->sockinfo == NULL);
      file->sockinfo = malloc(sizeof(km_fd_socket_t));
      km_assert(file->sockinfo != NULL);
      *file->sockinfo = *sockinfo;
   }
   return ret;
}

/*
 * deletes an existing guestfd to hostfd mapping (used by km_fs_close())
 *
 * Note: The caller must assure that the fd doesn't get re-used by another
 *       thread before this function complete. In km_close() this is handled
 *       by calling del_guest_fd before closing the fd itself.
 */
static inline void del_guest_fd(km_vcpu_t* vcpu, int fd)
{
   km_assert(fd >= 0 && fd < km_fs()->nfdmap);
   km_file_t* file = &km_fs()->guest_files[fd];
   km_assert(km_is_file_used(file) != 0);
   // file->error != 0 means socket was connected when snapshot was taken. We do not want to count that
   if (light_snap_accept_timeout != 0 && file->how == KM_FILE_HOW_ACCEPT && file->error == 0) {
      km_set_inactive_accept();   // account for accepted socket closing
   }

   if (__atomic_exchange_n(&file->inuse, 0, __ATOMIC_SEQ_CST) != 0) {
      file->ops = NULL;
      if (file->name != NULL) {
         free(file->name);
         file->name = NULL;
      }
   }
   if (file->sockinfo != NULL) {
      free(file->sockinfo);
      file->sockinfo = NULL;
   }
   km_fs_event_t* eventp;
   while ((eventp = TAILQ_FIRST(&file->events)) != NULL) {
      TAILQ_REMOVE(&file->events, eventp, link);
      free(eventp);
   }
   km_fd_close_dup(fd);
   if (file->ofd != -1) {
      km_file_t* other = &km_fs()->guest_files[file->ofd];
      file->ofd = -1;
      /*
       * We don't actually have a hold on other, so only do the update other->ofd
       * contains fd.
       */
      __atomic_compare_exchange_n(&other->ofd, &fd, -1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
   }
}

static inline int km_guestfd_error(km_vcpu_t* vcpu, int fd)
{
   return km_fs()->guest_files[fd].error;
}

char* km_guestfd_name(km_vcpu_t* vcpu, int fd)
{
   if (fd < 0 || fd >= km_fs()->nfdmap) {
      return NULL;
   }
   return km_fs()->guest_files[fd].name;
}

/*
 * maps a host fd to a guest fd. Returns a negative error number if mapping does not exist. Used by
 * SIGPIPE/SIGIO signal handlers and select. Note: vcpu is NULL if called from km signal handler.
 */
int km_fs_h2g_fd(int hostfd)
{
   if (hostfd < 0 || hostfd >= km_fs()->nfdmap) {
      return -ENOENT;
   }
   if (__atomic_load_n(&km_fs()->guest_files[hostfd].inuse, __ATOMIC_SEQ_CST) == 0) {
      return -ENOENT;
   }
   return hostfd;
}

/*
 * Translates guest fd to host fd. Returns negative errno if mapping does not exist.
 */
int km_fs_g2h_fd(int fd, km_file_ops_t** ops)
{
   if (fd < 0 || fd >= km_fs()->nfdmap) {
      return -1;
   }
   if (__atomic_load_n(&km_fs()->guest_files[fd].inuse, __ATOMIC_SEQ_CST) == 0) {
      return -1;
   }
   if (ops != NULL) {
      *ops = km_fs()->guest_files[fd].ops;
   }
   return fd;
}

int km_fs_max_guestfd()
{
   return km_fs()->nfdmap;
}

// int open(char *pathname, int flags, mode_t mode)
uint64_t km_fs_open(km_vcpu_t* vcpu, char* pathname, int flags, mode_t mode)
{
   char buf[PATH_MAX];
   km_file_ops_t* ops;

   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), &ops);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   int hostfd = __syscall_3(SYS_open, (uintptr_t)pathname, flags, mode);
   int guestfd;
   if (hostfd >= 0) {
      guestfd = km_add_guest_fd_internal(vcpu, hostfd, pathname, flags, KM_FILE_HOW_OPEN, ops);
   } else {
      guestfd = hostfd;
   }
   km_infox(KM_TRACE_FILESYS, "open(%s, %d, %o) - %d", pathname, flags, mode, guestfd);
   return guestfd;
}

// sanity check - dirfd, if needed, within guest range, and getdents_g2h not set on it
int km_fs_at(int dirfd, const char* const pathname)
{
   km_file_ops_t* ops;

   if (dirfd != AT_FDCWD && pathname[0] != '/') {
      if (km_fs_g2h_fd(dirfd, &ops) < 0) {
         return -EBADF;
      }
      if (ops != NULL && ops->getdents_g2h != NULL) {
         return -EINVAL;   // no fs_at with base in /proc and such
      }
   }
   return 0;
}

uint64_t km_fs_openat(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, mode_t mode)
{
   int ret = km_fs_at(dirfd, pathname);
   if (ret < 0) {
      return ret;
   }

   char buf[PATH_MAX];
   km_file_ops_t* ops;
   ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), &ops);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   int hostfd = __syscall_4(SYS_openat, dirfd, (uintptr_t)pathname, flags, mode);
   int guestfd;
   if (hostfd >= 0) {
      guestfd = km_add_guest_fd_internal(vcpu, hostfd, pathname, flags, KM_FILE_HOW_OPEN, ops);
   } else {
      guestfd = hostfd;
   }
   km_infox(KM_TRACE_FILESYS, "openat(%s, %d, %o) - %d", pathname, flags, mode, guestfd);
   return guestfd;
}

// int close(fd)
uint64_t km_fs_close(km_vcpu_t* vcpu, int fd)
{
   km_infox(KM_TRACE_FILESYS, "close(%d)", fd);
   if (km_fs_g2h_fd(fd, NULL) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   /*
    * Notes on closing guestfd:
    *
    * The "Dealing with error returns from close()" section
    * of the close(2) man page states: "Linux kernel always releases the
    * file descriptor early in the close operation, freeing it for reuse".
    * (And when they say 'always' they mean always. While close(2) can return
    * an error, the file descriptor is unconditionally closed).
    *
    * Hence the hostfd/guestfd mappings are cleared unconditionally
    * before close(2) is called.
    */
   del_guest_fd(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_1(SYS_close, fd);
   if (ret != 0) {
      km_warn(" error return from close of guest fd %d", fd);
   }
   return ret;
}

// int flock(int fd, int operation);
uint64_t km_fs_flock(km_vcpu_t* vcpu, int fd, int op)
{
   km_infox(KM_TRACE_FILESYS, "flock(%d, %d)", fd, op);
   if (km_fs_g2h_fd(fd, NULL) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_2(SYS_flock, fd, op);
   if (ret != 0) {
      km_warn(" error return from flock of guest fd %d", fd);
   }
   return ret;
}

// int shutdown(int sockfd, int how);
int km_fs_shutdown(km_vcpu_t* vcpu, int sockfd, int how)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_2(SYS_shutdown, host_fd, how);
   return ret;
}

// ssize_t read(int fd, void *buf, size_t count);
// ssize_t write(int fd, const void *buf, size_t count);
// ssize_t pread(int fd, void *buf, size_t count, off_t offset);
// ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
uint64_t km_fs_prw(km_vcpu_t* vcpu, int scall, int fd, void* buf, size_t count, off_t offset)
{
   int host_fd;
   km_file_ops_t* ops;
   km_infox(KM_TRACE_FILESYS, "%s(fd %d, buf %p count %ld, offset %ld)", km_hc_name_get(scall), fd, buf, count, offset);
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL && ops->read_g2h != NULL && (scall == SYS_read || scall == SYS_pread64)) {
      if (scall == SYS_pread64 && offset != 0) {
         km_warnx("unsupported %s", km_hc_name_get(scall));
         return -EINVAL;
      }
      ret = ops->read_g2h(host_fd, buf, count);
   } else {
      ret = __syscall_4(scall, host_fd, (uintptr_t)buf, count, offset);
   }
   return ret;
}

// ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
// ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
// ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
// ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
uint64_t
km_fs_prwv(km_vcpu_t* vcpu, int scall, int fd, const struct iovec* guest_iov, size_t iovcnt, off_t offset)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL && ops->read_g2h != NULL && (scall == SYS_readv || scall == SYS_preadv)) {
      km_warnx("unsupported %s", km_hc_name_get(scall));
      return -EINVAL;
   }
   // ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
   // ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
   // ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
   // ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
   //
   // need to convert not only the address of iov,
   // but also pointers to individual buffers in it
   struct iovec iov[iovcnt];
   for (int i = 0; i < iovcnt; i++) {
      iov[i].iov_base = km_gva_to_kma((long)guest_iov[i].iov_base);
      iov[i].iov_len = guest_iov[i].iov_len;
   }
   ret = __syscall_4(scall, host_fd, (uintptr_t)iov, iovcnt, offset);
   return ret;
}

// int ioctl(int fd, unsigned long request, void *arg);
uint64_t km_fs_ioctl(km_vcpu_t* vcpu, int fd, unsigned long request, void* arg)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_3(SYS_ioctl, host_fd, request, (uintptr_t)arg);
   return ret;
}

// int fcntl(int fd, int cmd, ... /* arg */ );
uint64_t km_fs_fcntl(km_vcpu_t* vcpu, int fd, int cmd, uint64_t arg)
{
   int host_fd;
   km_file_ops_t* ops;

   km_infox(KM_TRACE_FILESYS, "fd %d, cmd 0x%x, arg 0x%lx", fd, cmd, arg);
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   uint64_t farg = arg;
   if (cmd == F_SETLK || cmd == F_SETLKW || cmd == F_GETLK) {
      farg = (uint64_t)km_gva_to_kma(arg);
   } else if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
      // Let kernel pick hostfd destination. Satisfy the fd number request for guest below.
      farg = arg;
   }
   ret = __syscall_3(SYS_fcntl, host_fd, cmd, farg);
   if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
      if (ret >= 0) {
         km_file_t* file = &km_fs()->guest_files[fd];
         if (file->sockinfo != NULL) {
            ret = km_dup_socket_fd(vcpu,
                                   ret,
                                   km_guestfd_name(vcpu, fd),
                                   (cmd == F_DUPFD) ? 0 : O_CLOEXEC,
                                   file->sockinfo,
                                   file->how);
         } else {
            ret = km_add_guest_fd_internal(vcpu,
                                           ret,
                                           km_guestfd_name(vcpu, fd),
                                           (cmd == F_DUPFD) ? 0 : O_CLOEXEC,
                                           file->how,
                                           ops);
         }
         km_fs_add_to_dup_data(ret, host_fd);
      }
   }
   return ret;
}

// off_t lseek(int fd, off_t offset, int whence);
uint64_t km_fs_lseek(km_vcpu_t* vcpu, int fd, off_t offset, int whence)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   off_t ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL && ops->read_g2h != NULL && whence != SEEK_SET && offset != 0) {
      /*
       * File was matched on open with read op overwritten. Currently there are two,
       * /proc/(self|getpid())/sched. It's hard to keep track of file pointer, and nobody does lseek
       * other than rewind/lseek(0)
       */
      km_warnx("unsupported lseek on %s", km_guestfd_name(vcpu, fd));
      return -EINVAL;
   }
   ret = __syscall_3(SYS_lseek, host_fd, offset, whence);
   return ret;
}

// int getdents(unsigned int fd, struct linux_dirent *dirp, unsigned int count);
uint64_t km_fs_getdents(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count)
{
   int host_fd;
   km_file_ops_t* ops;

   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   long ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL && ops->getdents32_g2h != NULL) {
      ret = ops->getdents32_g2h(host_fd, dirp, count);
   } else {
      ret = __syscall_3(SYS_getdents, host_fd, (uintptr_t)dirp, count);
   }
   return ret;
}

// int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
uint64_t km_fs_getdents64(km_vcpu_t* vcpu, int fd, void* dirp, unsigned int count)
{
   int host_fd;
   km_file_ops_t* ops;

   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL && ops->getdents_g2h != NULL) {
      ret = ops->getdents_g2h(host_fd, dirp, count);
   } else {
      ret = __syscall_3(SYS_getdents64, host_fd, (uintptr_t)dirp, count);
   }
   return ret;
}

// int symlink(const char *target, const char *linkpath);
uint64_t km_fs_symlink(km_vcpu_t* vcpu, char* target, char* linkpath)
{
   int ret = km_fs_g2h_filename(linkpath, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad linkpath %s in symlink", linkpath);
      return -EINVAL;
   }
   ret = km_fs_g2h_filename(target, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad target %s in symlink", target);
      return -EINVAL;
   }
   ret = __syscall_2(SYS_symlink, (uintptr_t)target, (uintptr_t)linkpath);

   return ret;
}

// int link(const char *oldpath, const char *newpath);
uint64_t km_fs_link(km_vcpu_t* vcpu, char* old, char* new)
{
   int ret = km_fs_g2h_filename(old, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad oldpath %s in rename", old);
      return -EINVAL;
   }
   ret = km_fs_g2h_filename(new, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad new %s in rename", new);
      return -EINVAL;
   }
   ret = __syscall_2(SYS_link, (uintptr_t)old, (uintptr_t) new);
   return ret;
}

/*
 * check if pathname is in "/proc/self/fd/" or "/proc/`getpid()`/fd", process if it is.
 * Return 0 if doesn't match, negative for error, positive for result strlen
 */
static int proc_self_fd_name(const char* pathname, char* buf, size_t bufsz)
{
   int fd;
   if (sscanf(pathname, PROC_SELF_FD, &fd) != 1 && sscanf(pathname, proc_pid_fd, &fd) != 1) {
      return -ENOENT;
   }
   if (fd >= km_fs()->nfdmap) {
      return -ENOENT;
   }
   return 0;
}

/*
 * check if pathname is in "/proc/self/exe" or "/proc/`getpid()`/exe", process if it is.
 * Return 0 if doesn't match, negative for error, positive for result strlen
 */
static int proc_self_exe_name(const char* pathname, char* buf, size_t bufsz)
{
   strncpy(buf, km_guest.km_filename, bufsz);
   int ret = strlen(km_guest.km_filename);
   if (ret > bufsz) {
      ret = bufsz;
   }
   return ret;
}

// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
uint64_t km_fs_readlink(km_vcpu_t* vcpu, char* pathname, char* buf, size_t bufsz)
{
   ssize_t ret;

   /*
    * If we can't we handle request from internal tables and the actual readlink is ok,
    * handle special case - for link pointing to KM executable return payload's argv[0]
    */
   if ((ret = km_fs_g2h_readlink(pathname, buf, bufsz)) == 0 &&
       (ret = __syscall_3(SYS_readlink, (uintptr_t)pathname, (uintptr_t)buf, bufsz)) > 0) {
      char tmp[PATH_MAX + 1];   // reusable buffer for readlink and realpath
      buf[ret] = 0;             // make null terminated string
      int next_ret = __syscall_3(SYS_readlink, (uintptr_t)buf, (uintptr_t)tmp, PATH_MAX);

      if (next_ret == -EINVAL && realpath(buf, tmp) != NULL && strncmp(km_my_exec, tmp, PATH_MAX) == 0) {
         strncpy(buf, km_guest.km_filename, bufsz);
         if ((ret = strlen(km_guest.km_filename)) > bufsz) {
            ret = bufsz;
         }
      }
   }
   if (ret < 0) {
      km_infox(KM_TRACE_FILESYS, "%s : %s", pathname, strerror(-ret));
   } else {
      km_infox(KM_TRACE_FILESYS, "%s -> %.*s", pathname, ret, buf);
   }
   return ret;
}

// ssize_t readlinkat(it dirfd, const char *pathname, char *buf, size_t bufsiz);
uint64_t km_fs_readlinkat(km_vcpu_t* vcpu, int dirfd, char* pathname, char* buf, size_t bufsz)
{
   ssize_t ret;

   if ((ret = km_fs_g2h_readlink(pathname, buf, bufsz)) == 0) {
      if ((ret = km_fs_at(dirfd, pathname)) < 0) {
         return ret;
      }
      ret = __syscall_4(SYS_readlinkat, dirfd, (uintptr_t)pathname, (uintptr_t)buf, bufsz);
   }
   if (ret < 0) {
      km_infox(KM_TRACE_FILESYS, "%s : %s", pathname, strerror(-ret));
   } else {
      km_infox(KM_TRACE_FILESYS, "%s -> %.*s", pathname, ret, buf);
   }
   return ret;
}

// int getcwd(char *buf, size_t size);
uint64_t km_fs_getcwd(km_vcpu_t* vcpu, char* buf, size_t bufsz)
{
   int ret = __syscall_2(SYS_getcwd, (uintptr_t)buf, bufsz);
   return ret;
}

// int chdir(const char *path);
uint64_t km_fs_chdir(km_vcpu_t* vcpu, char* pathname)
{
   int ret = km_fs_g2h_filename(pathname, NULL, 0, NULL);
   if (ret != 0) {
      km_warnx("bad pathname %s in chdir", pathname);
      return -EINVAL;
   }
   ret = __syscall_1(SYS_chdir, (uintptr_t)pathname);
   return ret;
}

// int fchdir(int fd);
uint64_t km_fs_fchdir(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   km_file_ops_t* ops;

   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL) {
      km_warnx("bad fd in fchdir");
      return -EINVAL;   // no fchdir with base in /proc and such
   }
   ret = __syscall_1(SYS_fchdir, host_fd);
   return ret;
}

// int truncate(const char *path, off_t length);
uint64_t km_fs_truncate(km_vcpu_t* vcpu, char* pathname, off_t length)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_truncate, (uintptr_t)pathname, length);
   return ret;
}

// int ftruncate(int fd, off_t length);
uint64_t km_fs_ftruncate(km_vcpu_t* vcpu, int fd, off_t length)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_2(SYS_ftruncate, host_fd, length);
   return ret;
}

// int fsync(int fd);
uint64_t km_fs_fsync(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_1(SYS_fsync, host_fd);
   return ret;
}

// int fdatasync(int fd);
uint64_t km_fs_fdatasync(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_1(SYS_fdatasync, host_fd);
   return ret;
}

// int mkdir(const char *path, mode_t mode);
uint64_t km_fs_mkdir(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_mkdir, (uintptr_t)pathname, mode);
   return ret;
}

// int rmdir(const char *path);
uint64_t km_fs_rmdir(km_vcpu_t* vcpu, char* pathname)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_1(SYS_rmdir, (uintptr_t)pathname);
   return ret;
}

// int unlink(const char *path);
uint64_t km_fs_unlink(km_vcpu_t* vcpu, char* pathname)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_1(SYS_unlink, (uintptr_t)pathname);
   return ret;
}

// int unlinkat(int dirfd, const char *pathname, int flags);
uint64_t km_fs_unlinkat(km_vcpu_t* vcpu, int dirfd, const char* pathname, int flags)
{
   int ret = km_fs_at(dirfd, pathname);
   if (ret < 0) {
      return ret;
   }

   km_file_ops_t* ops;
   char buf[PATH_MAX];

   ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), &ops);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_3(SYS_unlinkat, dirfd, (uintptr_t)pathname, flags);
   km_infox(KM_TRACE_FILESYS, "unlinkat(%d, %s, 0x%x) returns %d", dirfd, pathname, flags, ret);
   return ret;
}

//  int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);
uint64_t
km_fs_utimensat(km_vcpu_t* vcpu, int dirfd, const char* pathname, struct timespec* ts, int flags)
{
   int ret;
   if (pathname != NULL) {
      ret = km_fs_at(dirfd, pathname);
      if (ret < 0) {
         return ret;
      }

      km_file_ops_t* ops;
      char buf[PATH_MAX];

      if ((ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), &ops)) < 0) {
         return ret;
      }
      if (ret > 0) {
         pathname = buf;
      }
   }
   ret = __syscall_4(SYS_utimensat, dirfd, (uint64_t)pathname, (uint64_t)ts, flags);
   km_infox(KM_TRACE_FILESYS, "utimensat(%d, %s, 0x%x) returns %d", dirfd, pathname, flags, ret);
   return ret;
}

// int mknod(const char *pathname, mode_t mode, dev_t dev);
uint64_t km_fs_mknod(km_vcpu_t* vcpu, char* pathname, mode_t mode, dev_t dev)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_3(SYS_mknod, (uintptr_t)pathname, mode, dev);
   return ret;
}

// int chown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_chown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_3(SYS_chown, (uintptr_t)pathname, uid, gid);
   return ret;
}

// int lchown(const char *pathname, uid_t owner, gid_t group);
uint64_t km_fs_lchown(km_vcpu_t* vcpu, char* pathname, uid_t uid, gid_t gid)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_3(SYS_lchown, (uintptr_t)pathname, uid, gid);
   return ret;
}

// int fchown(int fd, uid_t owner, gid_t group);
uint64_t km_fs_fchown(km_vcpu_t* vcpu, int fd, uid_t uid, gid_t gid)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_3(SYS_fchown, host_fd, uid, gid);
   return ret;
}

// int chmod(const char *pathname, mode_t mode);
uint64_t km_fs_chmod(km_vcpu_t* vcpu, char* pathname, mode_t mode)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_chmod, (uintptr_t)pathname, mode);
   return ret;
}

// int fchmod(int fd, mode_t mode);
uint64_t km_fs_fchmod(km_vcpu_t* vcpu, int fd, mode_t mode)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_2(SYS_fchmod, host_fd, mode);
   return ret;
}

// int rename(const char *roundup(strlenoldpath, const char *newpath);
uint64_t km_fs_rename(km_vcpu_t* vcpu, char* oldpath, char* newpath)
{
   int ret = km_fs_g2h_filename(oldpath, NULL, 0, NULL);
   if (ret < 0) {
      return ret;
   }
   ret = km_fs_g2h_filename(newpath, NULL, 0, NULL);
   if (ret < 0) {
      return ret;
   }
   ret = __syscall_2(SYS_rename, (uintptr_t)oldpath, (uintptr_t)newpath);
   return ret;
}

// int stat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_stat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_stat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int lstat(const char *pathname, struct stat *statbuf);
uint64_t km_fs_lstat(km_vcpu_t* vcpu, char* pathname, struct stat* statbuf)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_lstat, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
uint64_t
km_fs_statx(km_vcpu_t* vcpu, int dirfd, char* pathname, int flags, unsigned int mask, void* statxbuf)
{
   int ret = km_fs_at(dirfd, pathname);
   if (ret < 0) {
      return ret;
   }

   char buf[PATH_MAX];

   ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_5(SYS_statx, dirfd, (uintptr_t)pathname, flags, mask, (uintptr_t)statxbuf);
   return ret;
}

// int fstat(int fd, struct stat *statbuf);
uint64_t km_fs_fstat(km_vcpu_t* vcpu, int fd, struct stat* statbuf)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_2(SYS_fstat, host_fd, (uintptr_t)statbuf);
   if (km_trace_enabled() != 0) {
      char* file_name = km_guestfd_name(vcpu, fd);
      km_infox(KM_TRACE_FILESYS, "%s guest fd=%d hostfd=%d ret=%d", file_name, fd, host_fd, ret);
   }

   return ret;
}

// int statfs(int fd, struct statfs *statbuf);
uint64_t km_fs_statfs(km_vcpu_t* vcpu, char* pathname, struct statfs* statbuf)
{
   uint64_t ret = __syscall_2(SYS_statfs, (uintptr_t)pathname, (uintptr_t)statbuf);
   return ret;
}

// int fstatfs(int fd, struct statfs *statbuf);
uint64_t km_fs_fstatfs(km_vcpu_t* vcpu, int fd, struct statfs* statbuf)
{
   int host_fd;
   if ((host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   uint64_t ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_2(SYS_fstatfs, host_fd, (uintptr_t)statbuf);
   return ret;
}

// int access(const char *pathname, int mode);
uint64_t km_fs_access(km_vcpu_t* vcpu, const char* pathname, int mode)
{
   char buf[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_2(SYS_access, (uintptr_t)pathname, mode);
   return ret;
}

// int faccessat(int dirfd, const char *pathname, int mode, int flags);
uint64_t km_fs_faccessat(km_vcpu_t* vcpu, int dirfd, const char* pathname, int mode, int flags)
{
   int ret = km_fs_at(dirfd, pathname);
   if (ret < 0) {
      return ret;
   }
   char buf[PATH_MAX];
   ret = km_fs_g2h_filename(pathname, buf, sizeof(buf), NULL);
   if (ret < 0) {
      return ret;
   }
   if (ret > 0) {
      pathname = buf;
   }
   ret = __syscall_4(SYS_faccessat, dirfd, (uintptr_t)pathname, mode, flags);
   return ret;
}

// int dup(int oldfd);
uint64_t km_fs_dup(km_vcpu_t* vcpu, int fd)
{
   int host_fd;
   km_file_ops_t* ops;
   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }
   char* name = km_guestfd_name(vcpu, fd);
   km_assert(name != NULL);
   ret = __syscall_1(SYS_dup, host_fd);
   if (ret >= 0) {
      km_file_t* file = &km_fs()->guest_files[host_fd];
      if (file->sockinfo != NULL) {
         ret = km_dup_socket_fd(vcpu, ret, name, 0, file->sockinfo, file->how);
      } else {
         ret = km_add_guest_fd_internal(vcpu, ret, name, 0, file->how, ops);
      }
      km_fs_add_to_dup_data(ret, host_fd);
   }
   km_infox(KM_TRACE_FILESYS, "dup(%d) - %d", fd, ret);
   return ret;
}

// int dup3(int oldfd, int newfd, int flags);
uint64_t km_fs_dup3(km_vcpu_t* vcpu, int fd, int newfd, int flags)
{
   if (fd == newfd) {
      return -EINVAL;
   }
   if ((flags & ~O_CLOEXEC) != 0) {
      return -EINVAL;
   }
   if (newfd < 0 || newfd >= km_fs()->nfdmap) {
      return -EBADF;
   }

   int host_fd;
   km_file_ops_t* ops;

   if ((host_fd = km_fs_g2h_fd(fd, &ops)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, fd);
   if (ret != 0) {
      return ret;
   }

   char* name = km_guestfd_name(vcpu, fd);
   km_assert(name != NULL);
   ret = __syscall_3(SYS_dup3, host_fd, newfd, flags);
   if (ret >= 0) {
      if (km_is_file_used(&km_fs()->guest_files[ret]) != 0) {
         del_guest_fd(vcpu, ret);
      }

      km_file_t* file = &km_fs()->guest_files[host_fd];
      if (file->sockinfo != NULL) {
         ret = km_dup_socket_fd(vcpu, ret, name, flags, file->sockinfo, file->how);
      } else {
         ret = km_add_guest_fd(vcpu, ret, name, flags, ops);
      }
      km_fs_add_to_dup_data(ret, host_fd);
   }
   km_infox(KM_TRACE_FILESYS, "dup3(%d, %d, 0x%x) - %d", fd, newfd, flags, ret);
   return ret;
}

// int dup2(int oldfd, int newfd);
uint64_t km_fs_dup2(km_vcpu_t* vcpu, int fd, int newfd)
{
   if (fd == newfd) {
      return fd;
   }
   return km_fs_dup3(vcpu, fd, newfd, 0);
}

// int pipe(int pipefd[2]);
uint64_t km_fs_pipe(km_vcpu_t* vcpu, int pipefd[2])
{
   int host_pipefd[2];
   int ret = __syscall_1(SYS_pipe, (uintptr_t)host_pipefd);
   if (ret == 0) {
      pipefd[0] =
          km_add_guest_fd_internal(vcpu, host_pipefd[0], NULL, O_RDONLY, KM_FILE_HOW_PIPE_0, NULL);
      pipefd[1] =
          km_add_guest_fd_internal(vcpu, host_pipefd[1], NULL, O_WRONLY, KM_FILE_HOW_PIPE_1, NULL);
      km_connect_files(vcpu, pipefd);
      km_infox(KM_TRACE_FILESYS, "pipefd's %d %d", pipefd[0], pipefd[1]);
   }
   return ret;
}

// int pipe2(int pipefd[2], int flags);
uint64_t km_fs_pipe2(km_vcpu_t* vcpu, int pipefd[2], int flags)
{
   int host_pipefd[2];
   int ret = __syscall_2(SYS_pipe2, (uintptr_t)host_pipefd, flags);
   if (ret == 0) {
      pipefd[0] =
          km_add_guest_fd_internal(vcpu, host_pipefd[0], NULL, flags | O_RDONLY, KM_FILE_HOW_PIPE_0, NULL);
      pipefd[1] =
          km_add_guest_fd_internal(vcpu, host_pipefd[1], NULL, flags | O_WRONLY, KM_FILE_HOW_PIPE_1, NULL);
      km_connect_files(vcpu, pipefd);
      km_infox(KM_TRACE_FILESYS, "pipefd's %d %d", pipefd[0], pipefd[1]);
   }
   return ret;
}

// int eventfd2(unsigned int initval, int flags);
uint64_t km_fs_eventfd2(km_vcpu_t* vcpu, int initval, int flags)
{
   int ret = __syscall_2(SYS_eventfd2, initval, flags);
   if (ret >= 0) {
      ret = km_add_guest_fd_internal(vcpu, ret, NULL, 0, KM_FILE_HOW_EVENTFD, NULL);
   }
   return ret;
}

// int socket(int domain, int type, int protocol);
uint64_t km_fs_socket(km_vcpu_t* vcpu, int domain, int type, int protocol)
{
   int ret = __syscall_3(SYS_socket, domain, type, protocol);
   if (ret >= 0) {
      ret = km_add_socket_fd(vcpu, ret, NULL, 0, domain, type, protocol, KM_FILE_HOW_SOCKET);
   }
   return ret;
}

// int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
uint64_t
km_fs_getsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_5(SYS_getsockopt, host_sockfd, level, optname, (uintptr_t)optval, (uintptr_t)optlen);
   return ret;
}

// int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
uint64_t
km_fs_setsockopt(km_vcpu_t* vcpu, int sockfd, int level, int optname, void* optval, socklen_t optlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   ret = __syscall_5(SYS_setsockopt, host_sockfd, level, optname, (uintptr_t)optval, optlen);
   return ret;
}

// ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
// ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
uint64_t km_fs_sendrecvmsg(km_vcpu_t* vcpu, int scall, int sockfd, struct msghdr* msg, int flag)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   if (scall == SYS_sendmsg) {
      // translate sent file descriptors if any
      if (msg->msg_control != NULL) {
         for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
               int fdcount = (cmsg->cmsg_len - sizeof(struct cmsghdr)) / sizeof(int);
               km_infox(KM_TRACE_FILESYS,
                        "sockfd %d: processing SCM_RIGHTS send chunk containing %d fd's",
                        sockfd,
                        fdcount);
               int* guest_fd = (int*)CMSG_DATA(cmsg);
               for (int i = 0; i < fdcount; i++) {
                  int host_fd = km_fs_g2h_fd(guest_fd[i], NULL);
                  km_infox(KM_TRACE_FILESYS, "fd[%d] send guest fd %d as host fd %d", i, guest_fd[i], host_fd);
                  guest_fd[i] = host_fd;
               }
            }
         }
      }
   }
   ret = __syscall_3(scall, host_sockfd, (uintptr_t)msg, flag);
   if (scall == SYS_recvmsg && ret >= 0) {
      // receive file descriptors if any
      if (msg->msg_control != NULL) {
         for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
               int fdcount = (cmsg->cmsg_len - sizeof(struct cmsghdr)) / sizeof(int);
               km_infox(KM_TRACE_FILESYS,
                        "sockfd %d: processing SCM_RIGHTS recv chunk containing %d fd's",
                        sockfd,
                        fdcount);
               int* host_fd = (int*)CMSG_DATA(cmsg);
               for (int i = 0; i < fdcount; i++) {
                  int guest_fd =
                      km_add_guest_fd_internal(vcpu, host_fd[i], NULL, flag, KM_FILE_HOW_RECVMSG, NULL);
                  km_infox(KM_TRACE_FILESYS,
                           "fd[%d]: received host fd %d will be guest fd %d",
                           i,
                           host_fd[i],
                           guest_fd);
                  host_fd[i] = guest_fd;
               }
            }
         }
      }
   }
   return ret;
}

uint64_t km_fs_sendrecvmmsg(km_vcpu_t* vcpu,
                            int scall,
                            int sockfd,
                            struct mmsghdr* msgvec,
                            unsigned int vlen,
                            int flag,
                            struct timespec* timeout)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   if (scall == SYS_recvmmsg) {
      ret = __syscall_5(scall, sockfd, (uintptr_t)msgvec, vlen, flag, (uintptr_t)timeout);
   } else {
      ret = __syscall_4(scall, sockfd, (uintptr_t)msgvec, vlen, flag);
   }
   return ret;
}

// ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
uint64_t km_fs_sendfile(km_vcpu_t* vcpu, int out_fd, int in_fd, off_t* offset, size_t count)
{
   int host_outfd, host_infd;
   km_file_ops_t* ops;
   if ((host_outfd = km_fs_g2h_fd(out_fd, NULL)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, out_fd);
   if (ret != 0) {
      return ret;
   }
   if ((host_infd = km_fs_g2h_fd(in_fd, &ops)) < 0) {
      return -EBADF;
   }
   ret = km_guestfd_error(vcpu, in_fd);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL && ops->read_g2h != NULL) {
      km_warnx("bad fd in sendfile");
      return -EINVAL;
   }
   ret = __syscall_4(SYS_sendfile, host_outfd, host_infd, (uintptr_t)offset, count);
   return ret;
}

// ssize_t copy_file_range(int fd_in, off_t *off_in, int fd_out, loff_t *off_out, size_t len,
// unsigned int flags);
uint64_t km_fs_copy_file_range(
    km_vcpu_t* vcpu, int fd_in, off_t* off_in, int fd_out, off_t* off_out, size_t len, unsigned int flags)
{
   int host_outfd, host_infd;
   km_file_ops_t* ops;
   if ((host_outfd = km_fs_g2h_fd(fd_out, NULL)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, fd_out);
   if (ret != 0) {
      return ret;
   }
   if ((host_infd = km_fs_g2h_fd(fd_in, &ops)) < 0) {
      return -EBADF;
   }
   ret = km_guestfd_error(vcpu, fd_out);
   if (ret != 0) {
      return ret;
   }
   if (ops != NULL && ops->read_g2h != NULL) {
      km_warnx("bad fd in copyfilerange");
      return -EINVAL;
   }
   ret = __syscall_6(SYS_copy_file_range,
                     host_infd,
                     (uintptr_t)off_in,
                     host_outfd,
                     (uintptr_t)off_out,
                     len,
                     flags);
   return ret;
}

// int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
// int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t
km_fs_get_sock_peer_name(km_vcpu_t* vcpu, int hc, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   km_infox(KM_TRACE_NETWORK, "fd %d", sockfd);
   ret = __syscall_3(hc, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret == 0) {
      km_info_sockaddr(KM_TRACE_NETWORK, (hc == SYS_getpeername) ? "peername" : "sockname", addr);
   }
   return ret;
}

// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_bind(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   km_infox(KM_TRACE_NETWORK, "fd %d", sockfd);
   ret = __syscall_3(SYS_bind, host_sockfd, (uintptr_t)addr, addrlen);
   if (ret == 0) {
      km_fd_socket_t* sock = km_fs()->guest_files[sockfd].sockinfo;
      sock->addrlen = addrlen;
      memcpy(sock->addr, addr, addrlen);
      sock->state = KM_SOCK_STATE_BIND;
   }
   km_info_sockaddr(KM_TRACE_NETWORK, "bind addr", addr);
   return ret;
}

// int listen(int sockfd, int backlog)
uint64_t km_fs_listen(km_vcpu_t* vcpu, int sockfd, int backlog)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   km_infox(KM_TRACE_NETWORK, "fd %d, backlog %d", sockfd, backlog);
   ret = __syscall_2(SYS_listen, host_sockfd, backlog);
   if (ret == 0) {
      km_fd_socket_t* sock = km_fs()->guest_files[sockfd].sockinfo;
      sock->state = KM_SOCK_STATE_LISTEN;
      sock->backlog = backlog;
   }
   return ret;
}

// int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
uint64_t km_fs_accept4(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen, int fl)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   int hostfd;
   int accept_fd;
   if ((accept_fd = km_snap_find_accepted(sockfd)) >= 0) {
      // return the connection accepted in light_snap_listen().
      hostfd = dup(accept_fd);
      close(accept_fd);
      if (addr != NULL && addrlen != NULL) {
         if (getpeername(hostfd, addr, addrlen) < 0) {
            return -errno;
         }
      }
      if (fl != 0) {
         int tmp = fcntl(hostfd, F_GETFL, NULL);
         fcntl(hostfd, F_SETFL, tmp | (fl & (SOCK_NONBLOCK | SOCK_CLOEXEC)));
      }
   } else {
      if ((hostfd = __syscall_4(SYS_accept4, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen, fl)) < 0) {
         return hostfd;
      }
   }
   if (hostfd < 0) {
      return hostfd;
   }
   km_fd_socket_t* lsock = km_fs()->guest_files[sockfd].sockinfo;
   int guestfd =
       km_add_socket_fd(vcpu, hostfd, NULL, 0, lsock->domain, lsock->type, lsock->protocol, KM_FILE_HOW_ACCEPT);
   if (guestfd < 0) {
      close(hostfd);
      return -ENOMEM;
   }
   km_fd_socket_t* sock = km_fs()->guest_files[guestfd].sockinfo;
   sock->state = KM_SOCK_STATE_ACCEPT;

   km_set_active_accept();

   km_infox(KM_TRACE_NETWORK,
            "sockfd %d, addr %p, addrlen %p, fl 0x%x, accepted fd %d",
            sockfd,
            addr,
            addrlen,
            fl,
            guestfd);
   km_info_sockaddr(KM_TRACE_NETWORK, "accept4 peer", addr);

   return guestfd;
}

// int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
uint64_t km_fs_accept(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
   return km_fs_accept4(vcpu, sockfd, addr, addrlen, 0);
}

// int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
uint64_t km_fs_connect(km_vcpu_t* vcpu, int sockfd, struct sockaddr* addr, socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   km_infox(KM_TRACE_NETWORK, "fd %d", sockfd);
   km_info_sockaddr(KM_TRACE_NETWORK, "connect to", addr);
   ret = __syscall_3(SYS_connect, host_sockfd, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret >= 0) {
      km_fd_socket_t* sock = km_fs()->guest_files[sockfd].sockinfo;
      sock->state = KM_SOCK_STATE_CONNECT;
   }
   return ret;
}

// int socketpair(int domain, int type, int protocol, int sv[2]);
uint64_t km_fs_socketpair(km_vcpu_t* vcpu, int domain, int type, int protocol, int sv[2])
{
   int host_sv[2];
   int ret = __syscall_4(SYS_socketpair, domain, type, protocol, (uintptr_t)host_sv);
   if (ret == 0) {
      sv[0] =
          km_add_socket_fd(vcpu, host_sv[0], NULL, 0, domain, type, protocol, KM_FILE_HOW_SOCKETPAIR0);
      sv[1] =
          km_add_socket_fd(vcpu, host_sv[1], NULL, 0, domain, type, protocol, KM_FILE_HOW_SOCKETPAIR1);
      km_connect_files(vcpu, sv);
   }
   return ret;
}

// ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
//                const struct sockaddr *dest_addr, socklen_t addrlen);
uint64_t km_fs_sendto(km_vcpu_t* vcpu,
                      int sockfd,
                      const void* buf,
                      size_t len,
                      int flags,
                      const struct sockaddr* addr,
                      socklen_t addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   km_infox(KM_TRACE_NETWORK,
            "sockfd %d, buf %p, len %lu, flags 0x%x, dest_addr %p, addrlen %d",
            sockfd,
            buf,
            len,
            flags,
            addr,
            addrlen);
   km_info_mem(KM_TRACE_NETWORK, buf, len);
   ret = __syscall_6(SYS_sendto, host_sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, addrlen);
   return ret;
}

// ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//                  struct sockaddr *src_addr, socklen_t *addrlen);
uint64_t km_fs_recvfrom(
    km_vcpu_t* vcpu, int sockfd, void* buf, size_t len, int flags, struct sockaddr* addr, socklen_t* addrlen)
{
   int host_sockfd;
   if ((host_sockfd = km_fs_g2h_fd(sockfd, NULL)) < 0) {
      return -EBADF;
   }
   ssize_t ret = km_guestfd_error(vcpu, sockfd);
   if (ret != 0) {
      return ret;
   }
   km_infox(KM_TRACE_NETWORK,
            "fd %d, buf %p, len %lu, flags 0x%x, addr %p, addrlen %p",
            sockfd,
            buf,
            len,
            flags,
            addr,
            addrlen);
   ret =
       __syscall_6(SYS_recvfrom, host_sockfd, (uintptr_t)buf, len, flags, (uintptr_t)addr, (uintptr_t)addrlen);
   if (ret > 0) {
      km_info_mem(KM_TRACE_NETWORK, buf, ret);
   }
   return ret;
}

// returns 1 if the have a KM level error on a fd in a select set.
static int km_fs_select_error(km_vcpu_t* vcpu, int nfds, fd_set* fds)
{
   if (fds == NULL) {
      return 0;
   }
   for (int i = 0; i < nfds; i++) {
      if (FD_ISSET(i, fds)) {
         km_file_t* file = &km_fs()->guest_files[i];
         if (km_is_file_used(file) != 0 && file->error != 0) {
            FD_ZERO(fds);
            FD_SET(i, fds);
            return 1;
         }
      }
   }
   return 0;
}

static int
km_fs_check_select_errors(km_vcpu_t* vcpu, int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds)
{
   if (km_fs_select_error(vcpu, nfds, exceptfds) != 0) {
      FD_ZERO(readfds);
      FD_ZERO(writefds);
      return 1;
   }
   if (km_fs_select_error(vcpu, nfds, readfds) != 0) {
      FD_ZERO(exceptfds);
      FD_ZERO(writefds);
      return 1;
   }
   if (km_fs_select_error(vcpu, nfds, writefds) != 0) {
      FD_ZERO(exceptfds);
      FD_ZERO(readfds);
      return 1;
   }
   return 0;
}

uint64_t km_fs_pselect6(km_vcpu_t* vcpu,
                        int nfds,
                        fd_set* readfds,
                        fd_set* writefds,
                        fd_set* exceptfds,
                        struct timespec* timeout,
                        km_pselect6_sigmask_t* sigp)
{
   int ret;
   // If there is a KM level file error on any of the fds
   if ((ret = km_fs_check_select_errors(vcpu, nfds, readfds, writefds, exceptfds)) != 0) {
      return ret;
   }

   // Handle pre-snapshot recovery listening games
   // Seems like we will need a mutex to handle races between select() and accept().
   for (int i = 0; i < km_snap_listening_state_cnt; i++) {
      if (km_snap_listening_state_p[i].accept_fd >= 0 && readfds != NULL &&
          FD_ISSET(km_snap_listening_state_p[i].listen_fd, readfds)) {
         // pselect() wants to know if our listen fd has a new pending connection.
         FD_ZERO(readfds);
         FD_SET(km_snap_listening_state_p[i].listen_fd, readfds);
         if (writefds != NULL) {
            FD_ZERO(writefds);
         }
         if (exceptfds != NULL) {
            FD_ZERO(exceptfds);
         }
         return 1;
      }
   }

   // Account for sigmask changes in vcpu
   km_sigset_t oldset;
   km_sigemptyset(&oldset);
   if (sigp != NULL) {
      oldset = vcpu->sigmask;
      vcpu->sigmask = *(km_sigset_t*)sigp->ss;
   }
   ret = __syscall_6(SYS_pselect6,
                     nfds,
                     (uintptr_t)readfds,
                     (uintptr_t)writefds,
                     (uintptr_t)exceptfds,
                     (uintptr_t)timeout,
                     (uintptr_t)sigp);
   // if there is a sigmask for the syscall, restore previous value.
   if (sigp != NULL) {
      vcpu->sigmask = oldset;
   }
   return ret;
}
uint64_t km_fs_select(km_vcpu_t* vcpu,
                      int nfds,
                      fd_set* readfds,
                      fd_set* writefds,
                      fd_set* exceptfds,
                      struct timeval* timeout)
{
   struct timespec ts;
   struct timespec* tsp = NULL;
   if (timeout != NULL) {
      ts.tv_sec = timeout->tv_sec;
      ts.tv_nsec = timeout->tv_usec * 1000;
      tsp = &ts;
   }
   return km_fs_pselect6(vcpu, nfds, readfds, writefds, exceptfds, tsp, NULL);
}

// int poll(struct pollfd *fds, nfds_t nfds, int timeout);
uint64_t km_fs_poll(km_vcpu_t* vcpu, struct pollfd* fds, nfds_t nfds, int timeout)
{
   for (int i = 0; i < nfds; i++) {
      if ((fds[i].fd != 0xffffffff) && (km_fs_g2h_fd(fds[i].fd, NULL) < 0)) {
         return -EBADF;
      }
      if (fds[i].events == 0 || (fds[i].events & POLLERR) != 0) {
         int ret = km_guestfd_error(vcpu, fds[i].fd);
         if (ret != 0) {
            km_infox(KM_TRACE_FILESYS, "fd %d, recover error %d", fds[i].fd, ret);
            fds[i].revents = POLLERR;
            return 1;
         }
      }
      if ((fds[i].events & POLLIN) != 0 && km_snap_is_listener(fds[i].fd) != 0 &&
          km_snap_listening_state_p[i].accept_fd >= 0) {
         fds[i].revents = POLLIN;
         return 1;
      }
   }
   int ret = __syscall_3(SYS_poll, (uintptr_t)fds, nfds, timeout);
   return ret;
}

// int syscall(SYS_ppoll, struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const
// sigset_t *sigmask, size_t sigsetsize);
uint64_t km_fs_ppoll(km_vcpu_t* vcpu,
                     struct pollfd* fds,
                     nfds_t nfds,
                     struct timespec* tmo_p,
                     sigset_t* sigmask,
                     size_t sigsetsize)
{
   for (int i = 0; i < nfds; i++) {
      if (km_fs_g2h_fd(fds[i].fd, NULL) < 0) {
         return -EBADF;
      }
      if (fds[i].events == 0 || (fds[i].events & POLLERR) != 0) {
         int ret = km_guestfd_error(vcpu, fds[i].fd);
         if (ret != 0) {
            fds[i].revents = POLLERR;
            return 1;
         }
      }
      if ((fds[i].events & POLLIN) != 0 && km_snap_is_listener(fds[i].fd) != 0 &&
          km_snap_listening_state_p[i].accept_fd >= 0) {
         fds[i].revents = POLLIN;
         return 1;
      }
   }
   int ret =
       __syscall_5(SYS_ppoll, (uintptr_t)fds, nfds, (uintptr_t)tmo_p, (uintptr_t)sigmask, sigsetsize);
   return ret;
}

/*
 * epoll handling
 */
static inline km_fs_event_t* km_fs_event_find(km_vcpu_t* vcpu, km_file_t* file, int fd)
{
   km_fs_event_t* event;
   TAILQ_FOREACH (event, &file->events, link) {
      if (event->fd == fd) {
         return event;
      }
   }
   return NULL;
}

static inline void
km_fs_event_add(km_vcpu_t* vcpu, km_file_t* file, int guestfd, struct epoll_event* event)
{
   km_assert(km_fs_event_find(vcpu, file, guestfd) == NULL);
   km_fs_event_t eval = {.fd = guestfd, .event = *event};
   km_fs_event_t* fevent = (km_fs_event_t*)calloc(1, sizeof(km_fs_event_t));
   *fevent = eval;
   TAILQ_INSERT_TAIL(&file->events, fevent, link);
}

static inline void
km_fs_event_mod(km_vcpu_t* vcpu, km_file_t* file, int guestfd, struct epoll_event* event)
{
   km_fs_event_t* fevent = km_fs_event_find(vcpu, file, guestfd);
   km_assert(fevent != NULL);
   fevent->event = *event;
}

static inline void
km_fs_event_del(km_vcpu_t* vcpu, km_file_t* file, int guestfd, struct epoll_event* event)
{
   km_fs_event_t* fevent = km_fs_event_find(vcpu, file, guestfd);
   km_assert(fevent != NULL);
   TAILQ_REMOVE(&file->events, fevent, link);
   free(fevent);
}

// int epoll_create1(int flags);
uint64_t km_fs_epoll_create1(km_vcpu_t* vcpu, int flags)
{
   int ret = -1;
   int hostfd = __syscall_1(SYS_epoll_create1, flags);
   if (hostfd >= 0) {
      ret = km_add_guest_fd_internal(vcpu, hostfd, NULL, 0, KM_FILE_HOW_EPOLLFD, NULL);
   }
   return ret;
}

// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
uint64_t km_fs_epoll_ctl(km_vcpu_t* vcpu, int epfd, int op, int fd, struct epoll_event* event)
{
   int host_epfd;
   int host_fd;

   if ((host_epfd = km_fs_g2h_fd(epfd, NULL)) < 0 || (host_fd = km_fs_g2h_fd(fd, NULL)) < 0) {
      return -EBADF;
   }
   int ret = __syscall_4(SYS_epoll_ctl, host_epfd, op, host_fd, (uintptr_t)event);
   if (ret == 0) {
      km_file_t* file = &km_fs()->guest_files[epfd];
      switch (op) {
         case EPOLL_CTL_ADD:
            km_fs_event_add(vcpu, file, fd, event);
            break;
         case EPOLL_CTL_MOD:
            km_fs_event_mod(vcpu, file, fd, event);
            break;
         case EPOLL_CTL_DEL:
            km_fs_event_del(vcpu, file, fd, event);
            break;
      }
   }
   return ret;
}

uint64_t km_fs_timerfd_create(km_vcpu_t* vcpu, int clockid, int flags)
{
   int ret = __syscall_2(SYS_timerfd_create, clockid, flags);
   if (ret >= 0) {
      ret = km_add_guest_fd_internal(vcpu, ret, NULL, flags, KM_FILE_HOW_TIMERFD, NULL);
   }
   return ret;
}

static inline int
km_fs_event_check_errors(km_vcpu_t* vcpu, km_file_t* file, struct epoll_event* events, int maxevents)
{
   km_fs_event_t* event;
   int errors = 0;
   TAILQ_FOREACH (event, &file->events, link) {
      km_file_t* efile = &km_fs()->guest_files[event->fd];
      if (km_is_file_used(efile) != 0 && efile->error != 0) {
         events[errors].events = EPOLLERR | EPOLLHUP;
         events[errors].data = event->event.data;
         if (++errors >= maxevents) {
            break;
         }
      }
   }
   return errors;
}

// int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
//  const sigset_t *sigmask);
uint64_t km_fs_epoll_pwait(km_vcpu_t* vcpu,
                           int epfd,
                           struct epoll_event* events,
                           int maxevents,
                           int timeout,
                           const sigset_t* sigmask,
                           int sigsetsize)
{
   int host_epfd;
   if ((host_epfd = km_fs_g2h_fd(epfd, NULL)) < 0) {
      return -EBADF;
   }

   km_file_t* file = &km_fs()->guest_files[epfd];
   for (int i = 0; i < km_snap_listening_state_cnt; i++) {
      if (km_snap_listening_state_p[i].accept_fd >= 0) {
         km_fs_event_t* eventp = km_fs_event_find(vcpu, file, km_snap_listening_state_p[i].listen_fd);
         if (eventp != NULL && (eventp->event.events & EPOLLIN) != 0) {
            events[0].data = eventp->event.data;
            events[0].events = EPOLLIN;
            return 1;
         }
      }
   }

   km_queue_sig_sleep(vcpu);
   int ret = __syscall_6(SYS_epoll_pwait,
                         host_epfd,
                         (uintptr_t)events,
                         maxevents,
                         timeout,
                         (uintptr_t)sigmask,
                         sigsetsize);
   km_dequeue_sig_sleep(vcpu);
   return ret;
}

// int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
uint64_t
km_fs_epoll_wait(km_vcpu_t* vcpu, int epfd, struct epoll_event* events, int maxevents, int timeout)
{
   return km_fs_epoll_pwait(vcpu, epfd, events, maxevents, timeout, NULL, 0);
}

// int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);
uint64_t km_fs_prlimit64(km_vcpu_t* vcpu,
                         pid_t pid,
                         int resource,
                         const struct rlimit* new_limit,
                         struct rlimit* old_limit)
{
   /*
    * TODO: A number of rlimit values are effected by KM and we want to virtualize. For example:
    *       RLIMIT_AS - Maximum address space size.
    *       RLIMIT_CORE - Maximum core size.
    *       RLIMIT_DATA - Maximum data a process can create.
    *       RLIMIT_NOFILE - Maximum number of files
    *       RLIMIT_NPROC - Maximum number of processes (should set to 1).
    *       RLIMIT_SIGPENDING - Maximum number of pending signals.
    *       RLIMIT_STACK - maximum size of process stack.
    */
   struct rlimit tmp;
   if (resource == RLIMIT_NOFILE && new_limit != NULL) {
      if (new_limit->rlim_cur > km_fs()->nfdmap) {
         return -EPERM;
      }
      tmp.rlim_cur += MAX_KM_FILES;
      tmp = *new_limit;
      new_limit = &tmp;
   }
   int ret = __syscall_4(SYS_prlimit64, pid, resource, (uintptr_t)new_limit, (uintptr_t)old_limit);
   if (resource == RLIMIT_NOFILE && old_limit != NULL) {
      old_limit->rlim_cur -= MAX_KM_FILES;
   }
   return ret;
}

/*
 * == Snapshot generation
 */

size_t km_fs_dup_notes_length(void)
{
   size_t ret = km_note_header_size(KM_NT_NAME) + sizeof(km_nt_dup_t);
   for (int i = 0; i < dup_data.size; i++) {
      ret += sizeof(km_nt_dup_grp_t) + dup_data.groups[i]->size * sizeof(km_nt_dup_fd_t);
   }
   return ret;
}

// Helper function to return the number of bytes in a pipe or connection
static int ioctlfionread(int fd)
{
   int bytesavailable = 0;
   if (ioctl(fd, FIONREAD, &bytesavailable) < 0) {
      km_err(1, "ioctl FIONREAD on fd %d failed", fd);
   }
   return bytesavailable;
}

/*
 * Compute how much space all of the notes will need.
 */
size_t km_fs_core_notes_length(void)
{
   ssize_t queuedbytes;
   size_t ret = 0;
   for (int i = 0; i < km_fs()->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (km_is_file_used(file) != 0) {
         if (file->how == KM_FILE_HOW_EVENTFD) {
            ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_file_t);
         } else if (file->how == KM_FILE_HOW_EPOLLFD) {
            ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_epollfd_t);
            km_fs_event_t* ptr;
            TAILQ_FOREACH (ptr, &file->events, link) {
               ret += sizeof(km_nt_event_t);
            }
         } else if (file->sockinfo == NULL) {
            queuedbytes = 0;
            if (file->how == KM_FILE_HOW_PIPE_0) {
               // We are looking at the read end of a pipe, find out how much data is queued
               queuedbytes = ioctlfionread(i);
            }
            ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_file_t) +
                   km_nt_file_padded_size(file->name) + km_nt_chunk_roundup(queuedbytes);
         } else {
            queuedbytes = 0;
            if (file->how == KM_FILE_HOW_SOCKETPAIR0 || file->how == KM_FILE_HOW_SOCKETPAIR1) {
               queuedbytes = ioctlfionread(i);
            }
            ret += km_note_header_size(KM_NT_NAME) + sizeof(km_nt_socket_t) +
                   km_nt_file_padded_size(file->name) + km_nt_chunk_roundup(queuedbytes);
         }
      }
   }
   return ret;
}

/*
 * check if there are reading ends of pipes with data
 */

int km_pipes(void)
{
   ssize_t queuedbytes = 0;

   for (int i = 0; i < km_fs()->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (km_is_file_used(file) != 0) {
         if (file->sockinfo == NULL) {
            if (file->how == KM_FILE_HOW_PIPE_0) {
               // We are looking at the read end of a pipe, find out how much data is queued
               queuedbytes = ioctlfionread(i);
               if (queuedbytes != 0) {
                  return 1;
               }
            }
         } else {
            if (file->how == KM_FILE_HOW_SOCKETPAIR0 || file->how == KM_FILE_HOW_SOCKETPAIR1) {
               queuedbytes = ioctlfionread(i);
               if (queuedbytes != 0) {
                  return 1;
               }
            }
         }
      }
   }
   return 0;
}

// Helper function to ensure we read all the bytes requested.
// We abort after 50 tries.
static inline int do_full_read(int fd, char* bufp, size_t bufl)
{
   int read_attempts = 0;
   ssize_t total_bytes_read = 0;
   while (total_bytes_read != bufl) {
      ssize_t bytes_read = read(fd, bufp + total_bytes_read, bufl - total_bytes_read);
      if (bytes_read < 0) {
         km_warn("read fd %d, %ld bytes failed", fd, bufl - total_bytes_read);
         return errno;
      }
      total_bytes_read += bytes_read;
      if (++read_attempts > 50) {
         // Don't get stuck doing unproductive reads
         km_warnx("After %d read attempts, failed to read %ld bytes from fd %d", read_attempts, bufl, fd);
         return EINVAL;
      }
   }
   return 0;
}

// Helper function to ensure we write all bytes requested.
// We abort after 50 tries.
static inline int do_full_write(int fd, char* bufp, size_t bufl)
{
   int write_attempts = 0;
   ssize_t total_bytes_written = 0;
   while (total_bytes_written != bufl) {
      ssize_t bytes_written = write(fd, bufp + total_bytes_written, bufl - total_bytes_written);
      if (bytes_written < 0) {
         km_warn("write fd %d, %ld bytes failed", fd, bufl - total_bytes_written);
         return errno;
      }
      total_bytes_written += bytes_written;
      if (++write_attempts > 50) {
         // Don't get stuck doing unproductive writes
         km_warnx("After %d write attempts, failed to write %ld bytes to fd %d", write_attempts, bufl, fd);
         return EINVAL;
      }
   }
   return 0;
}

/*
 * Read the data buffered in a pipe or socketpair and store it in buf
 * Arguments:
 *  buf - store the data buffered in a pipe/socketpair into this buffer.
 *    We know the buffer is big enough because the buffer for all of the
 *    elf notes is precomputed before we build the notes.
 *  length - amount of space available at what buf points to.
 *  fd - the fd we can read the buffered pipe data from.
 *  queuedbytes - the number of bytes queued in the pipe/socketpair.
 * Returns:
 *  0 - success
 *  != 0 - errno for the failure
 */
static inline int fs_core_save_pipe_contents(char* buf, size_t length, int fd, size_t queuedbytes)
{
   km_file_t* file = &km_fs()->guest_files[fd];
   km_assert(queuedbytes <= length);
   km_assert(fd >= 0);

   // Read the bytes queued in the pipe into the memory buf points to
   int rc = do_full_read(fd, buf, queuedbytes);
   if (rc != 0) {
      return rc;
   }

   if (file->ofd < 0) {
      // The write side of this pipe is closed.  So we need to recreate the pipe
      // to put the data back into the pipe buffer and then close the write end
      // after writing the data.
      // If any of this fails, we can not allow the payload to resume since we can't
      // recreate is half open pipe or sockpair.
      int fdpair[2];
      if (file->how == KM_FILE_HOW_SOCKETPAIR0 || file->how == KM_FILE_HOW_SOCKETPAIR1) {
         if (socketpair(file->sockinfo->domain, file->sockinfo->type, file->sockinfo->protocol, fdpair) <
             0) {
            km_err(1, "Unable to recreate half open socketpair, fd %d", fd);
         }
      } else {
         if (pipe2(fdpair, file->flags) < 0) {
            km_err(1, "Unable to recreate half open pipe, fd %d", fd);
         }
      }
      rc = do_full_write(fdpair[1], buf, queuedbytes);
      if (rc != 0) {
         km_err(1, "Unabled to put data back into half open pipe/socketpair, fd %d", fd);
      }
      close(fdpair[1]);
      if (dup2(fdpair[0], fd) < 0) {
         km_err(1, "Unable to dup2 for half open pipe, fd %d", fd);
      }
      close(fdpair[0]);
   } else {
      // both ends of the pipe are open, we can write the data back into the write end.
      // If this fails, we can't let the payload continue.
      rc = do_full_write(file->ofd, buf, queuedbytes);
      if (rc != 0) {
         km_errx(1, "Unable to write data back into pipe/socketpair, fd %d", fd);
      }
   }
   return rc;
}

static inline int
fs_core_write_nonsocket(char* buf, size_t length, km_file_t* file, int fd, size_t* sizep)
{
   int pipesize = 0;
   struct stat st = {};
   if (fstat(fd, &st) < 0) {
      km_warn("Can't take a snaphot, fstat failed fd=%d", fd);
      return errno;
   }

   size_t queuedbytes = 0;
   if (file->how == KM_FILE_HOW_PIPE_0) {
      // This is the read side of the pipe, get the number of bytes waiting to be read
      queuedbytes = ioctlfionread(fd);
      pipesize = fcntl(fd, F_GETPIPE_SZ);
   }
   km_infox(KM_TRACE_SNAPSHOT, "fd=%d %s, queuedbytes %ld", fd, file->name, queuedbytes);
   char* cur = buf;
   size_t remain = length;
   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_FILE,
                             sizeof(km_nt_file_t) + km_nt_file_padded_size(file->name) +
                                 km_nt_chunk_roundup(queuedbytes));
   km_nt_file_t* fnote = (km_nt_file_t*)cur;
   cur += sizeof(km_nt_file_t);
   fnote->size = sizeof(km_nt_file_t);
   fnote->fd = fd;
   fnote->flags = file->flags;
   fnote->how = file->how;
   fnote->mode = st.st_mode;
   fnote->pipesize = pipesize;
   if (fnote->mode & S_IFIFO) {
      fnote->data = file->ofd;   // default to ofd. override based on file type
   } else {
      fnote->data = lseek(fd, 0, SEEK_CUR);
      if (fnote->data == (off_t)-1 && errno != ESPIPE) {
         km_warn("lseek fd %d failed", fd);
         return errno;
      }
   }

   strcpy(cur, file->name);
   cur += km_nt_file_padded_size(file->name);

   // Save any data queued in the read size of the pipe into this elf note.
   if (queuedbytes > 0 && fd > 2) {
      int rc = fs_core_save_pipe_contents(cur, length - (cur - buf), fd, queuedbytes);
      if (rc != 0) {
         return rc;
      }
      fnote->datalength = queuedbytes;
      cur += km_nt_chunk_roundup(fnote->datalength);
   }

   *sizep = cur - buf;
   return 0;
}

/*
 * Handles sockets created by socket() and socketpair().
 */
static inline int fs_core_write_socket(char* buf, size_t length, km_file_t* file, int fd, size_t* sizep)
{
   struct stat st = {};
   if (fstat(fd, &st) < 0) {
      km_warn("Can't take a snaphot, fstat failed fd=%d", fd);   // TODO: return error
      return errno;
   }

   char* cur = buf;
   size_t remain = length;
   size_t queuedbytes = 0;

   km_assert(file->sockinfo != NULL);
   if (file->how == KM_FILE_HOW_SOCKETPAIR0 || file->how == KM_FILE_HOW_SOCKETPAIR1) {
      /*
       * We put queued data into the note for the read side of the socket pair's elf note.
       */
      queuedbytes = ioctlfionread(fd);
   }
   km_infox(KM_TRACE_SNAPSHOT, "fd=%d %s, how %d, queuedbytes %ld", fd, file->name, file->how, queuedbytes);
   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_SOCKET,
                             sizeof(km_nt_socket_t) + km_nt_chunk_roundup(file->sockinfo->addrlen) +
                                 km_nt_chunk_roundup(queuedbytes));
   km_nt_socket_t* fnote = (km_nt_socket_t*)cur;
   cur += sizeof(km_nt_socket_t);
   fnote->size = sizeof(km_nt_socket_t);
   fnote->fd = fd;
   fnote->domain = file->sockinfo->domain;
   fnote->type = file->sockinfo->type;
   fnote->protocol = file->sockinfo->protocol;
   fnote->how = file->how;
   fnote->other = file->ofd;

   fnote->addrlen = file->sockinfo->addrlen;
   if (file->sockinfo->addrlen > 0) {
      memcpy(cur, file->sockinfo->addr, file->sockinfo->addrlen);
   }
   fnote->datalength = 0;
   fnote->state = KM_NT_SKSTATE_OPEN;
   if (file->sockinfo->state == KM_SOCK_STATE_BIND) {
      fnote->state = KM_NT_SKSTATE_BIND;
   } else if (file->sockinfo->state == KM_SOCK_STATE_LISTEN) {
      fnote->state = KM_NT_SKSTATE_LISTEN;
   } else if (file->sockinfo->state != KM_SOCK_STATE_OPEN) {
      fnote->state = KM_NT_SKSTATE_ERROR;
   }
   fnote->backlog = file->sockinfo->backlog;
   cur += km_nt_chunk_roundup(file->sockinfo->addrlen);

   // Save data queued in the socket for this direction.
   if (queuedbytes > 0) {
      int rc = fs_core_save_pipe_contents(cur, length - (cur - buf), fd, queuedbytes);
      if (rc != 0) {
         return rc;
      }
      fnote->datalength = queuedbytes;
      cur += km_nt_chunk_roundup(fnote->datalength);
   }

   km_infox(KM_TRACE_SNAPSHOT,
            "fd:%d ISOCK: how=%d %s other=0x%x state=%d, datalength %ld",
            fnote->fd,
            fnote->how,
            file->name,
            fnote->other,
            fnote->state,
            fnote->datalength);
   *sizep = cur - buf;
   return 0;
}

/*
 * Open /proc/self/fdinfo/%d, read its contents, find the field named by
 * fieldname, convert its associated value to binary and return that value.
 * The passed fd should be an fd for an eventfd.
 * We expect the fdinfo file to look something like this:
 * [paulp@home km]$ sudo cat /proc/1022/fdinfo/3
 * pos:	0
 * flags:	02004002
 * mnt_id:	15
 * ino:	12162
 * eventfd-count:                0
 * eventfd-id: 4
 * [paulp@home km]$
 */
static inline int extract_fdinfo_field(int fd, char* fieldname, uint64_t* value)
{
   char procfile[128];
   char fdinfobuf[256];
   snprintf(procfile, sizeof(procfile), "/proc/self/fdinfo/%d", fd);
   int pfd = open(procfile, O_RDONLY);
   if (pfd < 0) {
      km_warn("Couldn't open eventfd proc file %s", procfile);
      return errno;
   }
   ssize_t readcount = read(pfd, fdinfobuf, sizeof(fdinfobuf));
   if (readcount < 0) {
      km_warn("read eventfd proc entry %s failed", procfile);
      close(pfd);
      return errno;
   }
   close(pfd);
   fdinfobuf[readcount] = 0;
   char* p = strstr(fdinfobuf, fieldname);
   if (p == NULL) {
      km_warnx("Couldn't find %s field in %s?", fieldname, procfile);
      return EINVAL;
   }
   p += strlen(fieldname);
   char* endptr;
   *value = strtoll(p, &endptr, 10);
   if (endptr == p) {
      km_warnx("Unable to convert %s to decimal?", p);
      return EINVAL;
   }
   return 0;
}

#define EVENTFD_COUNT "eventfd-count:"
static inline int
fs_core_write_eventfd(char* buf, size_t length, km_file_t* file, int fd, size_t* sizep)
{
   struct stat st = {};
   if (fstat(fd, &st) < 0) {
      km_warn("Can't take a snaphot, fstat failed fd=%d", fd);   // TODO: return error
      return errno;
   }

   char* cur = buf;
   size_t remain = length;

   km_infox(KM_TRACE_SNAPSHOT, "fd=%d eventfd", fd);
   cur += km_add_note_header(cur, remain, KM_NT_NAME, NT_KM_EVENTFD, sizeof(km_nt_file_t));
   km_nt_file_t* fnote = (km_nt_file_t*)cur;
   cur += sizeof(km_nt_file_t);
   fnote->size = sizeof(km_nt_file_t);
   fnote->fd = fd;
   fnote->flags = file->flags;

   uint64_t eventfd_count = 0;
   int rc = extract_fdinfo_field(fd, EVENTFD_COUNT, &eventfd_count);
   if (rc != 0) {
      return rc;
   }
   fnote->data = eventfd_count;

   *sizep = cur - buf;
   return 0;
}

static inline int
fs_core_write_epollfd(char* buf, size_t length, km_file_t* file, int fd, size_t* sizep)
{
   struct stat st;
   if (fstat(fd, &st) < 0) {
      km_warn("Can't take a snaphot, fstat failed fd=%d", fd);
      return errno;
   }

   char* cur = buf;
   size_t remain = length;
   km_fs_event_t* event;
   int nevent = 0;

   TAILQ_FOREACH (event, &file->events, link) {
      nevent++;
   }

   km_infox(KM_TRACE_SNAPSHOT, "fd=%d epollfd %s nevent=%d", fd, file->name, nevent);

   /*
    * Verify that there are no pending epoll events.
    * Note that this call to epoll_wait() may be triggered by an edge but since we don't
    * actually do anything based on this, the edge that triggered this is now gone and the
    * payload missed the edge.  And to add to this, we have failed the snapshot and the
    * payload must continue and it didn't get its event.
    * As far as I can tell there is no non-destructive way to find out if there are pending
    * epoll events.
    */
   struct epoll_event epollevent;
   int fdcount = epoll_wait(fd, &epollevent, 1, 0);
   if (fdcount < 0) {
      km_warn("epoll_wait( %d ) failed", fd);
      return errno;
   }
   if (fdcount > 0) {
      km_warnx("Can't perform snapshot, epoll fd %d has pending events %d", fd, fdcount);
      return EAGAIN;
   }

   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_EPOLLFD,
                             sizeof(km_nt_epollfd_t) + nevent * sizeof(km_nt_event_t));
   km_nt_epollfd_t fval = {
       .size = sizeof(km_nt_epollfd_t),
       .fd = fd,
       .flags = file->flags,
       .nevent = nevent,

   };
   km_nt_epollfd_t* fnote = (km_nt_epollfd_t*)cur;
   *fnote = fval;
   cur += sizeof(km_nt_epollfd_t);
   TAILQ_FOREACH (event, &file->events, link) {
      km_infox(KM_TRACE_SNAPSHOT, "  monitored event: fd=%d events=0x%x", event->fd, event->event.events);
      km_nt_event_t eval = {.fd = event->fd, .event = event->event.events, .data = event->event.data.u64};
      km_nt_event_t* nt_event = (km_nt_event_t*)cur;
      *nt_event = eval;
      cur += sizeof(km_nt_event_t);
   }

   *sizep = cur - buf;
   return 0;
}

size_t km_fs_core_dup_write(char* buf, size_t length)
{
   char* cur = buf;
   size_t remain = length;

   cur += km_add_note_header(cur,
                             remain,
                             KM_NT_NAME,
                             NT_KM_DUP_DATA,
                             km_fs_dup_notes_length() - km_note_header_size(KM_NT_NAME));
   km_nt_dup_t* dupnote = (km_nt_dup_t*)cur;
   cur += sizeof(km_nt_dup_t);
   dupnote->size = dup_data.size;
   for (int i = 0; i < dup_data.size; i++) {
      km_nt_dup_grp_t* dupgrp = (km_nt_dup_grp_t*)cur;
      cur += sizeof(km_nt_dup_grp_t);
      dupgrp->size = dup_data.groups[i]->size;
      for (int j = 0; j < dup_data.groups[i]->size; j++) {
         cur += sizeof(dupgrp->fds[j]);
         dupgrp->fds[j] = dup_data.groups[i]->fds[j];
      }
   }
   return cur - buf;
}

int km_fs_core_notes_write(char* buf, size_t length, size_t* sizep)
{
   char* cur = buf;
   size_t remain = length;
   int rc;

   for (int i = 0; i < km_fs()->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (km_is_file_used(file) != 0) {
         size_t sz = 0;
         if (file->how == KM_FILE_HOW_EPOLLFD) {
            rc = fs_core_write_epollfd(cur, remain, file, i, &sz);
         } else if (file->how == KM_FILE_HOW_EVENTFD) {
            rc = fs_core_write_eventfd(cur, remain, file, i, &sz);
         } else if (file->how == KM_FILE_HOW_TIMERFD) {
            rc = EOPNOTSUPP;
            km_warnx("Can't snapshot timerfd's yet, fd %d", i);
         } else if (file->sockinfo == NULL) {
            rc = fs_core_write_nonsocket(cur, remain, file, i, &sz);
         } else {
            // This includes normal connections and socketpair connections
            rc = fs_core_write_socket(cur, remain, file, i, &sz);
         }
         if (rc != 0) {
            return rc;
         }
         cur += sz;
      }
   }
   *sizep = cur - buf;
   return 0;
}

/*
 * == Snapshot recovery
 */
static inline void km_fs_recover_fd(int guestfd, int hostfd, int flags, char* name, int ofd, int how)
{
   km_file_t* file = &km_fs()->guest_files[guestfd];

   if (guestfd < 0) {
      close(hostfd);
      return;
   }
   if (guestfd != hostfd) {
      if (guestfd != dup2(hostfd, guestfd)) {
         km_warn("can not dup2 %s to %d", name, guestfd);
         return;
      }
      close(hostfd);
   }

   km_set_file_used(file, 1);
   file->how = how;
   file->flags = flags;
   file->name = name;
   file->ofd = ofd;
   TAILQ_INIT(&file->events);
   km_infox(KM_TRACE_SNAPSHOT,
            "guestfd=%d hostfd=%d flags=0x%x name=%s ofd=%d how=%d",
            guestfd,
            hostfd,
            flags,
            name,
            ofd,
            how);
}

static inline int
km_fs_recover_fdpair(int guestfd[2], int hostfd[2], km_file_how_t how[2], int flags[2])
{
   km_infox(KM_TRACE_SNAPSHOT,
            "recover_fdpair: guest:%d,%d host:%d,%d how:%d,%d flags:%d,%d",
            guestfd[0],
            guestfd[1],
            hostfd[0],
            hostfd[1],
            how[0],
            how[1],
            flags[0],
            flags[1]);
   // Ensure we don't overwrite hostfd[1] in error.
   if (guestfd[0] >= machine.filesys->nfdmap) {
      km_warn("recover fd %d is invalid", guestfd[0]);
      return -1;
   }
   if (guestfd[1] >= machine.filesys->nfdmap) {
      km_warn("recover fd %d is invalid", guestfd[1]);
      return -1;
   }
   if (guestfd[0] == hostfd[1]) {
      km_fs_recover_fd(guestfd[1], hostfd[1], flags[1], km_get_nonfile_name(hostfd[1]), guestfd[0], how[1]);
      km_fs_recover_fd(guestfd[0], hostfd[0], flags[0], km_get_nonfile_name(hostfd[0]), guestfd[1], how[0]);
   } else {
      km_fs_recover_fd(guestfd[0], hostfd[0], flags[0], km_get_nonfile_name(hostfd[0]), guestfd[1], how[0]);
      km_fs_recover_fd(guestfd[1], hostfd[1], flags[1], km_get_nonfile_name(hostfd[1]), guestfd[0], how[1]);
   }
   return 0;
}

static inline int km_fs_recover_pipedata(km_nt_file_t* nt_file, int writefd, char* pipedata)
{
   if (nt_file->datalength > 0) {
      ssize_t byteswritten = write(writefd, pipedata, nt_file->datalength);
      if (byteswritten < 0) {
         km_warn("write queued pipe data to fd %d failed", writefd);
         return -1;
      }
      if (byteswritten != nt_file->datalength) {
         km_warnx("expected to write %ld bytes of pipe data, but wrote %ld bytes to fd %d",
                  nt_file->datalength,
                  byteswritten,
                  writefd);
         return -1;
      }
   }
   return 0;
}

static inline int km_fs_recover_pipe(km_nt_file_t* nt_file, char* name, char* pipedata)
{
   km_file_t* file = &km_fs()->guest_files[nt_file->fd];

   km_infox(KM_TRACE_SNAPSHOT,
            "recovering pipe: file used %d, flags=0x%x, fd %d, data %d, datalength %ld",
            km_is_file_used(file),
            nt_file->flags,
            nt_file->fd,
            nt_file->data,
            nt_file->datalength);

   if (km_is_file_used(file) != 0) {
      // This end of the pipe was create when the other end of the pipe's fd was created.
      km_assert(file->ofd == nt_file->data);

      // If this is the read side we may have buffered data in the elf note
      // that needs to be written into the write side of the pipe.
      // We cant do this here, the pipe's writefd may already be closed. !!!!!!!!!
      return (file->how == KM_FILE_HOW_PIPE_0) ? km_fs_recover_pipedata(nt_file, file->ofd, pipedata)
                                               : 0;
   }
   int hostfd[2];
   int syscall_flags = nt_file->flags & ~O_WRONLY;
   if (pipe2(hostfd, syscall_flags) < 0) {
      km_warn("pipe recover failure");
      return -1;
   }

   // If we are the read side of the pipe, write any buffered data from this elf note
   // into the write side of the pipe.  Do this now because the write end may be closed
   // below.
   if (nt_file->how == KM_FILE_HOW_PIPE_0) {
      if (km_fs_recover_pipedata(nt_file, hostfd[1], pipedata) != 0) {
         return -1;
      }
   }

   if (fcntl(hostfd[0], F_SETPIPE_SZ, nt_file->pipesize) < 0) {
      km_warn("set pipe buffer size, host fd %d", hostfd[0]);
      return -1;
   }

   /*
    * pipes are asymmetric. Make sure restored correctly.
    */
   int rfd = nt_file->fd;
   int wfd = nt_file->data;
   if ((nt_file->flags & O_WRONLY) != 0) {
      rfd = nt_file->data;
      wfd = nt_file->fd;
   }
   int guestfd[2] = {rfd, wfd};
   km_file_how_t how[2] = {KM_FILE_HOW_PIPE_0, KM_FILE_HOW_PIPE_1};
   int flags[2] = {syscall_flags, syscall_flags};

   if ((km_fs_recover_fdpair(guestfd, hostfd, how, flags)) != 0) {
      return -1;
   }
   return 0;
}

static inline int km_fs_recover_socket(km_nt_socket_t* nt_sock, struct sockaddr* addr, int addrlen)
{
   if (nt_sock->fd < 0 || nt_sock->fd >= machine.filesys->nfdmap) {
      km_warn("socket fd %d invalid", nt_sock->fd);
      return -1;
   }

   km_file_t* file = &km_fs()->guest_files[nt_sock->fd];
   km_assert(km_is_file_used(file) == 0);

   int host_fd;
   // check if this is lightweight snap listening socket, restore it accordingly
   if (km_snap_is_listener(nt_sock->fd) != 0) {
      host_fd = nt_sock->fd;
      if ((nt_sock->type & SOCK_NONBLOCK) == SOCK_NONBLOCK) {   // TODO: should we do SOCK_CLOEXEC
                                                                // here as well?
         int flags;
         if ((flags = fcntl(host_fd, F_GETFL, 0)) == -1 ||
             (fcntl(host_fd, F_SETFL, flags | FNDELAY)) == -1) {
            km_err(2, "fcntl FNDELAY failed");
         }
      }
   } else {
      if ((host_fd = socket(nt_sock->domain, nt_sock->type, nt_sock->protocol)) < 0) {
         km_warn("socket recover");
         return -1;
      }

      if (nt_sock->state == KM_SOCK_STATE_BIND || nt_sock->state == KM_SOCK_STATE_LISTEN) {
         // Force the SO_REUSEADDR setting.
         int flag = 1;
         if (setsockopt(host_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0) {
            km_warn("recover setsockopt(SO_REUSEADDR) failed");
            return -1;
         }
         struct sockaddr_in* sock4p = (struct sockaddr_in*)addr;
         km_infox(KM_TRACE_SNAPSHOT,
                  "binding fd %d to family %d, port %d, addr 0x%x",
                  host_fd,
                  sock4p->sin_family,
                  ntohs(sock4p->sin_port),
                  ntohl(sock4p->sin_addr.s_addr));
         if (bind(host_fd, addr, nt_sock->addrlen) < 0) {
            km_warn("recover bind failed");   // TODO: return error
            return -1;
         }
         if (nt_sock->state == KM_SOCK_STATE_LISTEN) {
            if (listen(host_fd, nt_sock->backlog) < 0) {
               km_warn("recover listen failed");
               return -1;
            }
         }
      }
   }
   km_fs_recover_fd(nt_sock->fd, host_fd, 0, km_get_nonfile_name(host_fd), -1, nt_sock->how);
   if (nt_sock->state == KM_NT_SKSTATE_ERROR) {
      file->error = -ECONNRESET;
      free(file->name);
      if ((file->name = strdup("socket error")) == NULL) {
         km_warn("No memory for socket error name");
         return -1;
      }
   }
   km_fd_socket_t sval = {
       .domain = nt_sock->domain,
       .type = nt_sock->type,
       .protocol = nt_sock->protocol,
   };
   km_fd_socket_t* sockinfo = (km_fd_socket_t*)malloc(sizeof(km_fd_socket_t));
   if (sockinfo == NULL) {
      km_err(2, "No memory for sockinfo");
      return -1;
   }
   *sockinfo = sval;
   if (addrlen > 0) {
      sockinfo->addrlen = addrlen;
      memcpy(sockinfo->addr, addr, addrlen);
   }
   file->sockinfo = sockinfo;

   return 0;
}

static int km_fs_recover_dup_data(char* ptr, size_t length)
{
   km_nt_dup_t* nt_dup = (km_nt_dup_t*)ptr;
   ptr += sizeof(km_nt_dup_t);
   dup_data.size = nt_dup->size;
   if (dup_data.size != 0 &&
       (dup_data.groups = malloc(dup_data.size * sizeof(dup_data.groups))) == NULL) {
      km_err(2, "No memory for dup_data.groups*");
      return -1;
   }
   for (int i = 0; i < dup_data.size; i++) {
      km_nt_dup_grp_t* nt_dup_grp = (km_nt_dup_grp_t*)ptr;
      ptr += sizeof(km_nt_dup_grp_t);
      km_fd_dup_grp_t* group = malloc(sizeof(km_fd_dup_grp_t));
      if (group == NULL) {
         km_err(2, "No memory for dup_data.groups");
         return -1;
      }
      dup_data.groups[i] = group;
      group->size = nt_dup_grp->size;
      if ((group->fds = malloc(group->size * sizeof(dup_data.groups[0]->fds[0]))) == NULL) {
         km_err(2, "No memory for dup_data.groups");
         return -1;
      }
      for (int i = 0; i < group->size; i++) {
         group->fds[i] = nt_dup_grp->fds[i];
      }

      ptr += sizeof(km_nt_dup_fd_t) * group->size;
   }
   return 0;
}

static int km_fs_recover_open_file(char* ptr, size_t length)
{
   km_nt_file_t* nt_file = (km_nt_file_t*)ptr;
   if (nt_file->size != sizeof(km_nt_file_t)) {
      km_warnx("nt_km_file_t size mismatch - old snapshot?");
      return -1;
   }
   char* name = ptr + sizeof(km_nt_file_t);
   char* pipedata = name + km_nt_file_padded_size(name);
   km_infox(KM_TRACE_SNAPSHOT,
            "fd=%d name=%s flags=0x%x mode=%o pos=%ld",
            nt_file->fd,
            name,
            nt_file->flags,
            nt_file->mode,
            nt_file->data);

   if (nt_file->fd < 0 || nt_file->fd >= machine.filesys->nfdmap) {
      km_warn("cannot recover invalid fd %d", nt_file->fd);
      return -1;
   }
   /*
    * If the std fds names are [std{in,out,err}] (as set in km_fs_init()) we inherit the fds from
    * km, otherwise process them in a regular way. Note the dup2 in below will close the km
    * inherited fd.
    */
   if ((nt_file->fd == 0 && strcmp(name, stdin_name) == 0) ||
       (nt_file->fd == 1 && strcmp(name, stdout_name) == 0) ||
       (nt_file->fd == 2 && strcmp(name, stderr_name) == 0)) {
      return 0;
   }

   if (nt_file->fd < 0 || nt_file->fd >= km_fs()->nfdmap) {
      km_warnx("bad file descriptor=%d", nt_file->fd);
      return -1;
   }
   int dup_fd = km_fs_check_for_dups_nolock(nt_file->fd);
   if (dup_fd >= 0) {
      if (km_fs_dup3(NULL, dup_fd, nt_file->fd, nt_file->flags & O_CLOEXEC) < 0) {
         return -1;
      }
      return 0;
   }

   if ((nt_file->mode & __S_IFMT) == __S_IFSOCK) {
      km_warnx("TODO: recover __S_ISOCK data=0x%lx", nt_file->data);
      return -1;
   }
   if ((nt_file->mode & __S_IFMT) == __S_IFIFO) {
      return km_fs_recover_pipe(nt_file, name, pipedata);
   }

   int fd = open(name, nt_file->flags, 0);
   if (fd < 0) {
      km_warn("can't open %s for fd %d", name, nt_file->fd);
      return -1;
   }

   struct stat st;
   if (fstat(fd, &st) < 0) {
      km_warn("can't fstat %s", name);
      return -1;
   }
   if (st.st_mode != nt_file->mode) {
      km_warnx("file mode mismatch expect 0%o got 0%o %s, payload fd %d",
               nt_file->mode,
               st.st_mode,
               name,
               nt_file->fd);
      return -1;
   }

   km_fs_recover_fd(nt_file->fd, fd, nt_file->flags, strdup(name), nt_file->data, nt_file->how);
   if ((nt_file->mode & __S_IFMT) == __S_IFREG && nt_file->data != 0) {
      if (lseek(nt_file->fd, nt_file->data, SEEK_SET) != nt_file->data) {
         km_warn("lseek failed");
         return -1;
      }
   }
   return 0;
}

// return filename to open for the request for /proc/`getpid()`/sched
static int proc_sched_open(const char* guest_fn, char* host_fn, size_t host_fn_sz)
{
   return snprintf(host_fn, host_fn_sz, "%s%s", PROC_SELF, guest_fn + proc_pid_length);
}

// called on the read of /proc/self/sched - need to replace the first line
static int proc_sched_read(int fd, char* buf, size_t buf_sz)
{
   fd = dup(fd);
   FILE* fp = fdopen(fd, "r");
   if (fp == NULL) {
      if (fd >= 0) {
         close(fd);
      }
      return -errno;
   }
   char tmp[128];
   char* x = fgets(tmp, sizeof(tmp), fp);   // skip the first line
   (void)x;                                 // avoid gcc warn
   if (feof(fp)) {                          // second read, to make sure we are at end of file
      fclose(fp);
      return 0;
   }
   int ret = snprintf(buf,
                      buf_sz,
                      "%s (%u, #threads: %u)\n",
                      km_guest.km_filename,
                      machine.pid,
                      km_vcpu_run_cnt());
   ret += fread(buf + ret, 1, buf_sz - ret, fp);
   fclose(fp);
   return ret;
}

// called on the read of /proc/self/auxv - need to return the payload's auxv
static int proc_auxv_read(int fd, char* buf, size_t buf_sz)
{
   memcpy(buf, machine.auxv, buf_sz);
   return MIN(buf_sz, machine.auxv_size);
}

// called on the open of /proc/pid/cmdline
static int proc_cmdline_open(const char* name, char* buf, size_t bufsz)
{
   return snprintf(buf, bufsz, "%s%s", PROC_SELF, name + proc_pid_length);
}

static int proc_self_getdents32(int fd, /* struct linux_dirent* */ void* buf, size_t buf_sz)
{
   struct linux_dirent {
      unsigned long d_ino;     /* Inode number */
      unsigned long d_off;     /* Offset to next linux_dirent */
      unsigned short d_reclen; /* Length of this linux_dirent */
      char d_name[];           /* Filename (null-terminated) */
                               /* length is actually (d_reclen - 2 -
                                  offsetof(struct linux_dirent, d_name)) */
      /*
      char           pad;       // Zero padding byte
      char           d_type;    // File type (only since Linux
                                // 2.6.4); offset is (d_reclen - 1)
      */
   };
   int ret = __syscall_3(SYS_getdents, fd, (uint64_t)buf, buf_sz);
   struct linux_dirent* e;
   for (off64_t offset = 0; offset < ret; offset += e->d_reclen) {
      e = (struct linux_dirent*)(buf + offset);
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
         continue;
      }
      uint64_t fdno;
      if (sscanf(e->d_name, "%lu", &fdno) != 1 || fdno >= MAX_OPEN_FILES - MAX_KM_FILES) {
         // Stop before getting into the km area of the fd space
         return offset;
      }
   }
   return ret;
}

static int proc_self_getdents(int fd, /* struct linux_dirent64* */ void* buf, size_t buf_sz)
{
   struct linux_dirent64 {
      ino64_t d_ino;           /* 64-bit inode number */
      off64_t d_off;           /* 64-bit offset to next structure */
      unsigned short d_reclen; /* Size of this dirent */
      unsigned char d_type;    /* File type */
      char d_name[];           /* Filename (null-terminated) */
   };
   int ret = __syscall_3(SYS_getdents64, fd, (uint64_t)buf, buf_sz);
   struct linux_dirent64* e;
   for (off64_t offset = 0; offset < ret; offset += e->d_reclen) {
      e = (struct linux_dirent64*)(buf + offset);
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
         continue;
      }
      ino64_t ino;
      if (sscanf(e->d_name, "%lu", &ino) != 1 || ino >= MAX_OPEN_FILES - MAX_KM_FILES) {
         return offset;
      }
   }
   return ret;
}

/*
 * Table of pathnames to watch for and process specially. The patterns are processed by
 * km_fs_filename_init(), first by applying PID for /proc/%u pattern, then compile the pattern
 * into regex, to match pathname on filepaths ops like open, stat, readlink... Regular files
 * won't match, all of the file system ops are regular. If the name matches, some of the ops
 * might need to be done specially, eg /proc/self/sched content needs to be modified for read, or
 * /proc/self/fd getdents. ops is vector of these ops as well as name matching for open and
 * readlink. If name matches on open the returned ops is stored in fd translation structure
 * (km_file_t). When guest to host fd ctranslation is done the ops is also returned, and function
 * pointers used to alter the functionality of file system ops.
 */
static km_filename_table_t km_filename_table[] = {
    {
        .pattern = "^/proc/self/fd/[[:digit:]]+$",
        .ops = {.open_g2h = proc_self_fd_name, .readlink_g2h = proc_self_fd_name},
    },
    {
        .pattern = "^/proc/self/exe$",
        .ops = {.open_g2h = proc_self_exe_name, .readlink_g2h = proc_self_exe_name},
    },
    {
        .pattern = "^/proc/self/fd$",
        .ops = {.getdents_g2h = proc_self_getdents, .getdents32_g2h = proc_self_getdents32},
    },
    {
        .pattern = "^/proc/self/sched$",
        .ops = {.read_g2h = proc_sched_read},
    },
    {
        .pattern = "^/proc/self/auxv$",
        .ops = {.read_g2h = proc_auxv_read},
    },
    {
        .pattern = "^/proc/%u/fd/[[:digit:]]+$",
        .ops = {.open_g2h = proc_self_fd_name, .readlink_g2h = proc_self_fd_name},
    },
    {
        .pattern = "^/proc/%u/exe$",
        .ops = {.open_g2h = proc_self_exe_name, .readlink_g2h = proc_self_exe_name},
    },
    {
        .pattern = "^/proc/%u/fd$",
        .ops = {.getdents_g2h = proc_self_getdents, .getdents32_g2h = proc_self_getdents32},
    },
    {
        .pattern = "^/proc/%u/sched$",
        .ops = {.open_g2h = proc_sched_open, .read_g2h = proc_sched_read},
    },
    {
        .pattern = "^/proc/%u/cmdline$",
        .ops = {.open_g2h = proc_cmdline_open},
    },
    {},
};

// Given pointer to ops field from the table above, return line # in that table, or -1

#define container_of(ptr, type, member) ((type*)((char*)(ptr) - __builtin_offsetof(type, member)))
int km_filename_table_line(km_file_ops_t* o)
{
   if (o == NULL) {
      return -1;
   }
   return container_of(o, km_filename_table_t, ops) - km_filename_table;
}

// given line # retreive ops
km_file_ops_t* km_file_ops(int i)
{
   if (i < 0) {
      return NULL;
   }
   return &km_filename_table[i].ops;
}

/*
 * For now every entry in the table starts with "/proc". Most files wont match, so we check for
 * "/proc" first.
 */
static const_string_t km_filename_prefix = "/proc";
static const int km_filename_prefix_sz = 5;
/*
 * Check and translate if necessary the guest into host view of the file name, for example
 * "/proc/...", as needs to be done for open and friends (stat ...)
 *
 * Return: 0 if no match
 *        >0 for successful match, length of new name
 *        <0 (-errno) for match that shouldn't open
 *
 * Also return ops vector associated with the new fd to be used in subsequent file ops. For
 * instance, read_g2h() would massage the returned buffer for something like /proc/self/sched
 */
static int km_fs_g2h_filename(const char* name, char* buf, size_t bufsz, km_file_ops_t** ops)
{
   if (strncmp(km_filename_prefix, name, km_filename_prefix_sz) != 0) {
      if (ops != NULL) {
         *ops = NULL;
      }
      return 0;
   }
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      if (regexec(&t->regex, name, 0, NULL, 0) == 0) {
         if (ops != NULL) {
            *ops = &t->ops;
         }
         if (t->ops.open_g2h != NULL) {
            return t->ops.open_g2h(name, buf, bufsz);
         }
         strncpy(buf, name, bufsz);
         return 1;
      }
   }
   // no match, regular file
   if (ops != NULL) {
      *ops = NULL;
   }
   return 0;
}

/*
 * Check and translate if necessary the guest into host view of the file name, for example
 * "/proc/...", for readlinks
 * Return: 0 if no match
 *        <0 (-errno) for match that shouldn't work
 */
static int km_fs_g2h_readlink(const char* name, char* buf, size_t bufsz)
{
   if (strncmp(km_filename_prefix, name, km_filename_prefix_sz) != 0) {
      return 0;
   }
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      if (regexec(&t->regex, name, 0, NULL, 0) == 0) {
         if (t->ops.readlink_g2h == NULL) {
            return -ENOENT;
         }
         return t->ops.readlink_g2h(name, buf, bufsz);
      }
   }
   return 0;
}

/*
 * Process the table or filenames to watch for
 */
static void km_fs_filename_init(void)
{
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      if (strncmp(t->pattern + 1, PROC_PID, strlen(PROC_PID)) == 0) {   // +1 to skip ^
         char pat[128];
         snprintf(pat, sizeof(pat), t->pattern, machine.pid);
         if (regcomp(&t->regex, pat, REG_EXTENDED | REG_NOSUB) != 0) {
            km_err(1, "filename regcomp failed, exiting...");
         }
      } else {
         if (regcomp(&t->regex, t->pattern, REG_EXTENDED | REG_NOSUB) != 0) {
            km_err(1, "filename regcomp failed, exiting...");
         }
      }
   }
   if ((km_my_exec = realpath(PROC_SELF_EXE, NULL)) == NULL) {
      km_err(1, "realpath /proc/self/exe failed");
   }
}

static void km_fs_filename_fini(void)
{
   for (km_filename_table_t* t = km_filename_table; t->pattern != NULL; t++) {
      regfree(&t->regex);
   }
   free(km_my_exec);
}

int km_fs_init(void)
{
   struct rlimit lim;

   if (getrlimit(RLIMIT_NOFILE, &lim) < 0) {
      return -errno;
   }

   machine.filesys = calloc(1, sizeof(km_filesys_t));
   lim.rlim_cur = MAX_OPEN_FILES - MAX_KM_FILES;   // Limit max open files. TODO: config

   km_infox(KM_TRACE_FILESYS, "lim.rlim_cur=%ld", lim.rlim_cur);
   km_fs()->nfdmap = lim.rlim_cur;
   km_fs()->guest_files = calloc(lim.rlim_cur, sizeof(km_file_t));
   km_assert(km_fs()->guest_files != NULL);

   if (km_exec_recover_guestfd() != 0) {
      // parent invocation - setup guest std file streams.
      for (int i = 0; i < 3; i++) {
         km_file_t* file = &km_fs()->guest_files[i];
         km_assert(km_is_file_used(file) == 0);
         km_set_file_used(file, 1);
         file->ofd = -1;
         file->name = km_get_nonfile_name(i);
         int ispts = (strncmp(file->name, "/dev/pts/", 9) == 0);
         switch (i) {
            case 0:
               if (ispts != 0) {
                  free(file->name);
                  file->name = strdup(stdin_name);
               }
               file->flags = O_RDONLY;
               break;
            case 1:
               if (ispts != 0) {
                  free(file->name);
                  file->name = strdup(stdout_name);
               }
               file->flags = O_WRONLY;
               break;
            case 2:
               if (ispts != 0) {
                  free(file->name);
                  file->name = strdup(stderr_name);
               }
               file->flags = O_WRONLY;
               break;
         }
      }
   }
   snprintf(proc_pid_exe, sizeof(proc_pid_exe), PROC_PID_EXE, machine.pid);
   snprintf(proc_pid_fd, sizeof(proc_pid_fd), PROC_PID_FD, machine.pid);
   snprintf(proc_pid, sizeof(proc_pid), PROC_PID, machine.pid);
   proc_pid_length = strlen(proc_pid);
   km_fs_filename_init();
   return 0;
}

void km_fs_fini(void)
{
   km_fs_filename_fini();
   if (km_fs() == NULL) {
      return;
   }
   if (km_fs()->guest_files != NULL) {
      for (int i = 0; i < km_fs()->nfdmap; i++) {
         km_file_t* file = &km_fs()->guest_files[i];
         if (file->name != NULL) {
            free(file->name);
         }
         if (file->sockinfo != NULL) {
            free(file->sockinfo);
         }
      }
      free(km_fs()->guest_files);
   }
   free(machine.filesys);
}

/*
 * === KM internal fd management.
 */

static int internal_fd = KM_START_FDS;

void km_filesys_internal_fd_reset()
{
   internal_fd = KM_START_FDS;
}

// dup internal fd to km private area
int km_internal_fd(int fd, int km_fd)
{
   if (fd < 0) {
      return fd;
   }
   int newfd;
   if (km_fd == -1) {
      newfd = dup2(fd, __atomic_fetch_add(&internal_fd, 1, __ATOMIC_SEQ_CST));
      km_assert(newfd >= 0 && newfd < MAX_OPEN_FILES);
   } else {
      newfd = dup2(fd, km_fd);
   }
   if (newfd >= MAX_OPEN_FILES - MAX_KM_FILES) {
      close(fd);
   }
   return newfd;
}

int km_internal_open(const char* name, int flag, int mode)
{
   int fd = open(name, flag, mode);
   return km_internal_fd(fd, -1);
}

int km_internal_eventfd(unsigned int initval, int flags)
{
   int fd = eventfd(initval, flags);
   return km_internal_fd(fd, -1);
}

int km_internal_fd_ioctl(int fd, unsigned long request, ...)
{
   void* arg;
   va_list ap;
   va_start(ap, request);
   arg = va_arg(ap, void*);
   va_end(ap);

   int retfd = ioctl(fd, request, arg);
   return km_internal_fd(retfd, -1);
}

int km_gdb_listen(int domain, int type, int protocol)
{
   int fd = socket(domain, type, protocol);
   return km_internal_fd(fd, KM_GDB_LISTEN);
}

int km_gdb_accept(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
   int newfd = accept(fd, addr, addrlen);
   return km_internal_fd(newfd, KM_GDB_ACCEPT);
}

int km_mgt_listen(int domain, int type, int protocol)
{
   int fd = socket(domain, type, protocol);
   return km_internal_fd(fd, KM_MGM_LISTEN);
}

int km_mgt_accept(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
   int newfd = accept(fd, addr, addrlen);
   return km_internal_fd(newfd, KM_MGM_ACCEPT);
}

/*
 * Use the dup of fd 2 or a new one to open km_log_file in km dedicated fd area,
 * to be used for km logging.
 * We also must not hold open the payload's stderr if it is a pipe or socket.
 * The parent process may be waiting for the pipe or connection to close before
 * proceeding.
 * We also need to handle being cornered where we can't use stderr and we can't
 * open a file because the filesystem the container runs in is readonly.  So,
 * we must tolerate not being able to log at all.
 * TODO: support logging to syslog or journal.
 * name - the name of a file to log to. in addition name can be:
 *  NULL: causing us to continue to log to stderr if it is not a pipe.
 *  "none": causing no km logging.
 *  "stderr": causing km to log to stderr no matter what.  (This is useful for the
 *    bats tests)
 */
char km_nologging_reason[128];   // leave a clue to why there is no km logging
void km_redirect_msgs(const char* name)
{
   int fd, fd1;
   if (name != NULL) {
      if (strcmp(name, "stderr") == 0) {
         // If they ask, let them log to stderr no matter what.  Mostly useful for the bats tests.
         fd = dup2(2, KM_LOGGING);
      } else if (strcmp(name, "none") == 0) {
         // We need to be able to test having no logging at all.  km could run in a container
         // with read only filesystems and we may not be able to use stderr either.
         snprintf(km_nologging_reason, sizeof(km_nologging_reason), "logging turned off by request");
         km_tracex("change km logging to none by request");
         return;
      } else {
         fd1 = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
         fd = dup2(fd1, KM_LOGGING);
         close(fd1);
      }
   } else {
      struct stat statb;
      if (fstat(2, &statb) == 0) {
         if (S_ISFIFO(statb.st_mode) || S_ISSOCK(statb.st_mode)) {
            /*
             * We don't want to hold open pipes or sockets to the payload's stderr.
             * This interferes with the other end of the pipeline's ability to detect
             * that the pipe or socket has been closed by the payload.
             * If we can create and open /tmp/km_XXXXX.log then log there.  If we can't
             * create the log file, then /tmp could be on a readonly filesystem, so we don't log.
             */
            char filename[32];
            snprintf(filename, sizeof(filename), "/tmp/km_%d.log", getpid());
            km_tracex("Switch km logging to %s on first attempt to log", filename);
            km_trace_set_log_file_name(filename);
            return;
         } else {
            // stderr is not a pipeline the parent process could be waiting on
            fd = dup2(2, KM_LOGGING);
         }
      } else {
         // we don't know what stderr is.
         km_warn("Unable to stat stderr, km logging disabled, errno %d", errno);
         snprintf(km_nologging_reason,
                  sizeof(km_nologging_reason),
                  "couldn't fstat stderr, error %d",
                  errno);
         return;
      }
   }
   km_assert(fd == KM_LOGGING);

   if ((km_log_file = fdopen(fd, "w")) == NULL) {
      km_err(1, "Failed to redirect km log");
   }
   setlinebuf(km_log_file);
}

/*
 * Ensure km logging continues after an execve() call from the payload.
 */
void km_redirect_msgs_after_exec(void)
{
   struct stat statb;

   if (fstat(KM_LOGGING, &statb) == 0) {
      if ((km_log_file = fdopen(KM_LOGGING, "w")) == NULL) {
         km_err(1, "Failed to redirect km log");
      }
      setlinebuf(km_log_file);
   }
}

/*
 * Close stdin, stdout, and stderr FILE* but keep the file descriptors open for guest use. Guest
 * has its own stdio FILE* inside.
 */
void km_close_stdio(int log_to_fd)
{
   int fdin = dup(0);
   fclose(stdin);
   stdin = NULL;
   dup2(fdin, 0);
   close(fdin);

   int fdout, fderr;
   if (log_to_fd == -1) {
      fdout = dup(1);
      fderr = dup(2);
   } else {
      fdout = log_to_fd;
      fderr = log_to_fd;
   }
   fclose(stdout);
   fclose(stderr);
   stdout = NULL;
   stderr = NULL;
   dup2(fdout, 1);
   dup2(fderr, 2);
   if (log_to_fd == -1) {
      close(fdout);
      close(fderr);
   } else {
      close(log_to_fd);
   }
}

/*
 * Snapshot recovery for files
 */

static int km_fs_recover_socketdata(km_nt_socket_t* nt_sock, int writefd)
{
   if (nt_sock->datalength > 0) {
      char* p = (char*)nt_sock + sizeof(km_nt_socket_t) + km_nt_chunk_roundup(nt_sock->addrlen);
      ssize_t byteswritten = write(writefd, p, nt_sock->datalength);
      if (byteswritten < 0) {
         km_warn("write %ld bytes queued data to fd %d failed", nt_sock->fd, nt_sock->datalength);
         return -1;
      }
      if (byteswritten < nt_sock->datalength) {
         km_warnx("write to fd %d truncated, wrote %ld, expected to write %ld",
                  nt_sock->fd,
                  nt_sock->datalength,
                  byteswritten);
         return -1;
      }
   }
   return 0;
}

static int km_fs_recover_socketpair(km_nt_socket_t* nt_sock)
{
   km_infox(KM_TRACE_SNAPSHOT,
            "socketpair: fd=%d how=%d other=%d domain=%d type=%d protocol=%d, datalength %ld",
            nt_sock->fd,
            nt_sock->how,
            nt_sock->other,
            nt_sock->domain,
            nt_sock->type,
            nt_sock->protocol,
            nt_sock->datalength);

   if (km_is_file_used(&km_fs()->guest_files[nt_sock->fd]) != 0) {
      // Our fd is already created, just restore any data queued in the pipe.
      return km_fs_recover_socketdata(nt_sock, nt_sock->other);
   }

   int host_sv[2];
   if (socketpair(nt_sock->domain, nt_sock->type, nt_sock->protocol, host_sv) < 0) {
      km_warn("socketpair recovery failure");
      return -1;
   }

   /*
    * Restore any data buffered in the socket before calling km_fs_recover_fdpair()
    * because km_fs_recover_fdpair() will close the other end if this is a half
    * open socketpair.
    */
   if (km_fs_recover_socketdata(nt_sock,
                                nt_sock->how == KM_FILE_HOW_SOCKETPAIR0 ? host_sv[1] : host_sv[0]) < 0) {
      return -1;
   }

   /*
    * With guestfd == hostfd  dealing with the two file descriptors we got from
    * socketpair(2) recovery is a bit tricky. In particular, we need to handle
    * the case where the the 'other' fd was assigned the desired number for 'this'
    * fd. If that happens, we reassign the fd number for 'other' first.
    */
   int otherfd = nt_sock->other;
   if (nt_sock->how == KM_FILE_HOW_SOCKETPAIR0) {
      int guestfd[2] = {nt_sock->fd, otherfd};
      km_file_how_t how[2] = {KM_FILE_HOW_SOCKETPAIR0, KM_FILE_HOW_SOCKETPAIR1};
      int flags[2] = {0, 0};
      if (km_fs_recover_fdpair(guestfd, host_sv, how, flags) < 0) {
         return -1;
      }

   } else {
      int guestfd[2] = {otherfd, nt_sock->fd};
      km_file_how_t how[2] = {KM_FILE_HOW_SOCKETPAIR1, KM_FILE_HOW_SOCKETPAIR0};
      int flags[2] = {0, 0};
      if (km_fs_recover_fdpair(guestfd, host_sv, how, flags) < 0) {
         return -1;
      }
   }
   return 0;
}

static int km_fs_recover_open_socket(char* ptr, size_t length)
{
   km_nt_socket_t* nt_sock = (km_nt_socket_t*)ptr;
   if (nt_sock->size != sizeof(km_nt_socket_t)) {
      km_warnx("nt_km_socket_t size mismatch - old snapshot?");
      return -1;
   }
   int dup_fd = km_fs_check_for_dups_nolock(nt_sock->fd);
   if (dup_fd >= 0) {
      if (km_fs_dup2(NULL, dup_fd, nt_sock->fd) < 0) {
         return -1;
      }
      return 0;
   }
   if (nt_sock->how == KM_FILE_HOW_SOCKETPAIR0 || nt_sock->how == KM_FILE_HOW_SOCKETPAIR1) {
      return km_fs_recover_socketpair(nt_sock);
   }

   /*
    * Assume socket optionally bound for listening
    */
   km_infox(KM_TRACE_SNAPSHOT,
            "socket: fd=%d other=%d how=%d addrlen=%d",
            nt_sock->fd,
            nt_sock->other,
            nt_sock->how,
            nt_sock->addrlen);

   struct sockaddr* sa = (struct sockaddr*)(ptr + sizeof(km_nt_socket_t));
   km_fs_recover_socket(nt_sock, sa, nt_sock->addrlen);
   return 0;
}

static int km_fs_recover_eventfd(char* ptr, size_t length)
{
   km_nt_file_t* nt_file = (km_nt_file_t*)ptr;
   if (nt_file->size != sizeof(km_nt_file_t)) {
      km_warnx("nt_km_file_t size mismatch - old snapshot?");
      return -1;
   }
   km_infox(KM_TRACE_SNAPSHOT, "fd=%d eventfd", nt_file->fd);

   km_file_t* file = &km_fs()->guest_files[nt_file->fd];
   if (km_is_file_used(file) != 0) {
      km_errx(2, "eventfd file %d in use.", nt_file->fd);
   }
   int dup_fd = km_fs_check_for_dups_nolock(nt_file->fd);
   if (dup_fd >= 0) {
      if (km_fs_dup3(NULL, dup_fd, nt_file->fd, nt_file->flags & O_CLOEXEC) < 0) {
         return -1;
      }
      return 0;
   }

   int hostfd = eventfd(nt_file->data, nt_file->flags);
   if (hostfd < 0) {
      km_warn("eventfd failed, guest fd %d", nt_file->fd);
      return -1;
   }
   km_fs_recover_fd(nt_file->fd,
                    hostfd,
                    nt_file->flags,
                    km_get_nonfile_name(hostfd),
                    nt_file->data,
                    KM_FILE_HOW_EVENTFD);
   return 0;
}

static int km_fs_recover_epollfd(char* ptr, size_t length)
{
   km_nt_epollfd_t* nt_epollfd = (km_nt_epollfd_t*)ptr;

   km_infox(KM_TRACE_SNAPSHOT, "EPOLLFD fd=%d", nt_epollfd->fd);
   if (nt_epollfd->size != sizeof(km_nt_epollfd_t)) {
      km_warnx("nt_km_eventfd_t size mismatch - old snapshot?, got %d, expected %d",
               nt_epollfd->size,
               sizeof(km_nt_epollfd_t));
      return -1;
   }
   if (nt_epollfd->fd < 0 || nt_epollfd->fd >= machine.filesys->nfdmap) {
      km_warnx("cannot recover invalid event fd %d", nt_epollfd->fd);
      return -1;
   }

   km_file_t* file = &km_fs()->guest_files[nt_epollfd->fd];
   if (km_is_file_used(file) != 0) {
      km_errx(2, "file %d in use. %s", nt_epollfd->fd, file->name);
   }
   int dup_fd = km_fs_check_for_dups_nolock(nt_epollfd->fd);
   if (dup_fd >= 0) {
      if (km_fs_dup3(NULL, dup_fd, nt_epollfd->fd, nt_epollfd->flags & O_CLOEXEC) < 0) {
         return -1;
      }
      return 0;
   }

   int hostfd = epoll_create1(nt_epollfd->flags);
   if (hostfd < 0) {
      km_warn("epoll_create failed");
      return -1;
   }
   km_fs_recover_fd(nt_epollfd->fd, hostfd, 0, km_get_nonfile_name(hostfd), -1, KM_FILE_HOW_EPOLLFD);
   for (int i = 0; i < nt_epollfd->nevent; i++) {
      km_nt_event_t* nt_event = &nt_epollfd->events[i];
      int host_efd = km_fs_g2h_fd(nt_event->fd, NULL);
      if (host_efd < 0) {
         km_warnx("monitored fd=%d does not exist", nt_event->fd);
         return -1;
      }
      struct epoll_event ev = {.events = nt_event->event, .data.u64 = nt_event->data};
      if (epoll_ctl(nt_epollfd->fd, EPOLL_CTL_ADD, host_efd, &ev) < 0) {
         km_warn("epoll_ctl for fd=%d failed", nt_event->fd);
         return -1;
      }

      km_fs_event_t* event = calloc(1, sizeof(km_fs_event_t));
      event->fd = nt_event->fd;
      event->event.events = nt_event->event;
      event->event.data.u64 = nt_event->data;
      TAILQ_INSERT_TAIL(&file->events, event, link);
   }

   return 0;
}

int km_fs_recover(char* notebuf, size_t notesize)
{
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_DUP_DATA, km_fs_recover_dup_data) < 0) {
      km_errx(2, "recover open files failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_FILE, km_fs_recover_open_file) < 0) {
      km_errx(2, "recover open files failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_SOCKET, km_fs_recover_open_socket) < 0) {
      km_errx(2, "recover open sockets failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_EVENTFD, km_fs_recover_eventfd) < 0) {
      km_errx(2, "recover open eventfd's failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_EPOLLFD, km_fs_recover_epollfd) < 0) {
      km_errx(2, "recover open epollfd's failed");
   }
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_IOCONTEXTS, km_fs_recover_iocontexts) < 0) {
      km_errx(2, "recover iocontexts failed");
   }
   return 0;
}

/*
 * Go thru the open socket fd notes and find all of the listening sockets
 * in the snapshot.  Save that information in the global km_snap_listening_state.
 */
static int km_fs_harvest_listenfds(char* ptr, size_t length)
{
   km_nt_socket_t* nt_sock = (km_nt_socket_t*)ptr;
   if (nt_sock->size != sizeof(km_nt_socket_t)) {
      km_warnx("nt_km_socket_t size mismatch - old snapshot?");
      return -1;
   }
   if (nt_sock->state == KM_NT_SKSTATE_LISTEN) {
      if (km_snap_listening_state_cnt >= km_snap_listening_state_max) {
         if (km_snap_listening_state_max == 0) {
            km_snap_listening_state_max = 4;
         } else {
            km_snap_listening_state_max *= 2;
         }
         km_snap_listening_state_p =
             realloc(km_snap_listening_state_p,
                     sizeof(km_snap_listening_state_t) * km_snap_listening_state_max);
         if (km_snap_listening_state_p == NULL) {
            km_errx(2,
                    "Couldn't allocate %ld bytes for listening socket table",
                    sizeof(km_snap_listening_state_t) * km_snap_listening_state_max);
         }
      }
      km_snap_listening_state_t* p = &km_snap_listening_state_p[km_snap_listening_state_cnt++];
      p->listen_fd = nt_sock->fd;
      p->accept_fd = -1;
      p->listen_sockinfo.state = KM_SOCK_STATE_LISTEN;
      p->listen_sockinfo.backlog = nt_sock->backlog;
      p->listen_sockinfo.domain = nt_sock->domain;
      p->listen_sockinfo.type = nt_sock->type;
      p->listen_sockinfo.protocol = nt_sock->protocol;
      p->listen_sockinfo.addrlen = nt_sock->addrlen;
      memcpy(p->listen_sockinfo.addr, nt_sock + 1, nt_sock->addrlen);
   }
   km_infox(KM_TRACE_SNAPSHOT, "Found %d listening fd's in the snapshot", km_snap_listening_state_cnt);
   return 0;
}

// Free memory allocated by km_snapshot_listenfds_find()
static void km_snapshot_listenfds_free(void)
{
   free(km_snap_listening_state_p);
   km_snap_listening_state_p = NULL;
   km_snap_listening_state_max = 0;
   km_snap_listening_state_cnt = 0;
}

/*
 * Get a list of all listening socket fd's in the snapshot and produce a list
 * pointed to by km_snap_listening_state_p.
 * If an error occurs this function exits.
 */
static void km_snapshot_listenfds_find(km_elf_t* e)
{
   km_payload_t tmp_payload = {};
   km_snapshot_fill_km_payload(e, &tmp_payload);
   long save_offset = ftell(e->file);

   size_t notesize = 0;
   char* notebuf = km_snapshot_read_notes(fileno(e->file), &notesize, &tmp_payload);
   if (notebuf == NULL) {
      km_errx(2, "PT_NOTES not found");
   }
   if (fseek(e->file, save_offset, SEEK_SET) != 0) {
      km_err(2, "seek back to previous offset failed");
   }

   // Find the listening fd's
   if (km_snapshot_notes_apply(notebuf, notesize, NT_KM_SOCKET, km_fs_harvest_listenfds) < 0) {
      km_errx(2, "Couldn't find listening socket fd's");
   }

   free(notebuf);
   free(tmp_payload.km_phdr);
}

int km_fs_inotify_init(km_vcpu_t* vcpu)
{
   int hostfd = __syscall_0(SYS_inotify_init);
   if (hostfd >= 0) {
      hostfd = km_add_guest_fd_internal(vcpu, hostfd, NULL, 0, KM_FILE_HOW_WATCH, NULL);
   }
   return hostfd;
}

int km_fs_inotify_init1(km_vcpu_t* vcpu, int flags)
{
   int hostfd = __syscall_1(SYS_inotify_init1, flags);
   if (hostfd >= 0) {
      hostfd = km_add_guest_fd_internal(vcpu, hostfd, NULL, flags, KM_FILE_HOW_WATCH1, NULL);
   }
   return hostfd;
}

int km_fs_inotify_add_watch(km_vcpu_t* vcpu, int guest_fd, char* pathname, uint32_t mask)
{
   int host_fd = km_fs_g2h_fd(guest_fd, NULL);
   if (host_fd < 0) {
      return -EBADF;
   }

   char host_pathname[PATH_MAX];
   int ret = km_fs_g2h_filename(pathname, host_pathname, sizeof(host_pathname), NULL);
   if (ret < 0) {
      return ret;
   }
   uint64_t name = (ret == 0) ? (uint64_t)pathname : (uint64_t)host_pathname;

   ret = __syscall_3(SYS_inotify_add_watch, host_fd, name, mask);
   return ret;
}

int km_fs_inotify_rm_watch(km_vcpu_t* vcpu, int guest_fd, int wd)
{
   int host_fd = km_fs_g2h_fd(guest_fd, NULL);
   if (host_fd < 0) {
      return -EBADF;
   }
   int ret = __syscall_2(SYS_inotify_rm_watch, host_fd, wd);
   return ret;
}
