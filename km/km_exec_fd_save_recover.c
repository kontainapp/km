/*
 * Copyright © 2021 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */

/*
 * This file contains the functions used to save the current guest open file state
 * in a big string for used by exec() and then the functions used to unpack that
 * string and restore the guest open file state.
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
#include "km_filesys.h"
#include "km_filesys_private.h"
#include "km_gdb.h"
#include "km_mem.h"

// The types of fd's this code currently understands.
enum km_fdtype {
   KM_FDTYPE_FILE,
   KM_FDTYPE_PIPE,
   KM_FDTYPE_SOCKETPAIR,
   KM_FDTYPE_SOCKET,
   KM_FDTYPE_EVENTFD
};

/*
 * Turn a char array pointed at by bin for binlen bytes into ascii hex stored in hex.
 * hex is assumed to have enough space to hold (binlen * 2) + 1 bytes.
 * hex is nul terminated.
 */
static void km_bin2hex(unsigned char* bin, int binlen, char* hex)
{
   int i;
   for (i = 0; i < binlen; i++) {
      hex[i * 2] = "0123456789abcdef"[(bin[i] >> 4)];
      hex[(i * 2) + 1] = "0123456789abcdef"[bin[i] & 0xf];
   }
   hex[i * 2] = 0;
}

// Convert an ascii hex digit to a binary nibble.  Returns -1 for invalid hex digit.
static int xdigit2bin(char c)
{
   if (c >= '0' && c <= '9') {
      return c - '0';
   }
   if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
   }
   if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
   }
   return -1;
}

/*
 * Convert a string of ascii hex digits into a binary byte array of the represented value.
 * The conversion stops on the first invalid hex digit.  If there are an odd number of
 * valid hex digits, the final hex digit is discarded and not included in the result.
 * The count of bytes returned is stored in *binlen.
 */
static void km_hex2bin(char* hex, unsigned char* bin, int* binlen)
{
   int i;
   char nh, nl;
   *binlen = 0;
   for (i = 0;; i += 2) {
      nh = xdigit2bin(hex[i]);
      nl = xdigit2bin(hex[i + 1]);
      if (nh < 0 || nl < 0) {
         return;
      }
      bin[*binlen] = (nh << 4) | nl;
      (*binlen)++;
   }
}

/*
 * FD tracer function to assist with testing fd recovery following an execve() system call.
 * Since test programs may expect these trace message formats to be a certain way, if you
 * change the format change the tests to work with the new format.
 */
void km_exec_fdtrace(char* tag, int fd)
{
   km_file_t* file;
   km_fs_event_t* eventp;
   char buffer[512];   // should be big enough unless there are a zillion events on an epoll fd
   size_t l;

   if (km_trace_tag_enabled(KM_TRACE_EXEC) == 0) {
      // skip all of the following if trace is disabled
      return;
   }

   file = &km_fs()->guest_files[fd];
   if (file->inuse == 0) {
      return;
   }

   // All trace types start with this.
   snprintf(buffer, sizeof(buffer), "%s: fd %d, name %s, ", tag, fd, file->name);
   l = strlen(buffer);

   if (file->how == KM_FILE_HOW_EVENTFD) {   // epoll fd
      snprintf(&buffer[l], sizeof(buffer) - l, "epoll flags 0x%x, ", file->flags);
      TAILQ_FOREACH (eventp, &file->events, link) {
         l = strlen(buffer);
         snprintf(&buffer[l],
                  sizeof(buffer) - l,
                  "event %d,0x%x,0x%lx, ",
                  eventp->fd,
                  eventp->event.events,
                  eventp->event.data.u64);
      }
   } else if (file->sockinfo == NULL) {   // non-socket fd
      struct stat statb;
      if (fstat(fd, &statb) < 0) {
         km_info(KM_TRACE_EXEC, "No trace for fd %d, fstat failed", fd);
         return;
      }
      if (S_ISFIFO(statb.st_mode)) {   // pipe
         snprintf(&buffer[l],
                  sizeof(buffer) - l,
                  "pipe how %d, flags 0x%x, ofd %d",
                  file->how,
                  file->flags,
                  file->ofd);
      } else {   // plain file
         km_file_ops_t* ops;
         km_fs_g2h_fd(fd, &ops);
         snprintf(&buffer[l],
                  sizeof(buffer) - l,
                  "file how %d, flags 0x%x, index %d",
                  file->how,
                  file->flags,
                  km_filename_table_line(ops));
      }
   } else if (file->how == KM_FILE_HOW_SOCKETPAIR0 || file->how == KM_FILE_HOW_SOCKETPAIR1) {   // socketpair
      snprintf(&buffer[l], sizeof(buffer) - l, "socketpair how %d, ofd %d", file->how, file->ofd);
   } else {   // socket fd
      snprintf(&buffer[l],
               sizeof(buffer) - l,
               "socket how %d, state %d, backlog %d, ofd %d, domain %d, type %d, protocol %d, "
               "addrlen %d, sockaddr ",
               file->how,
               file->sockinfo->state,
               file->sockinfo->backlog,
               file->ofd,
               file->sockinfo->domain,
               file->sockinfo->type,
               file->sockinfo->protocol,
               file->sockinfo->addrlen);
      km_bin2hex((unsigned char*)file->sockinfo->addr, file->sockinfo->addrlen, &buffer[strlen(buffer)]);
   }
   km_infox(KM_TRACE_EXEC, "%s", buffer);
}

/*
 * Create an ascii string that represents the information about all of the open payload
 * fd's.  The reason we create this string is to pass on to an exec target program km payload.
 * The km running the target payload will restore its open fd table with this information.
 * Returns:
 *   address of the allocated string, the caller must free this.
 *   NULL - there was a failure while producing the string.
 */
char* km_exec_save_fd(char* varname)
{
   char* env_value = NULL;
   char* new_env_value;
   char* more_env_value;
   int i;

   for (i = 0; i < machine.filesys->nfdmap; i++) {
      km_file_t* file = &km_fs()->guest_files[i];
      if (file->inuse == 0) {
         continue;
      }

      // skip files that are close on exec()
      int fdflags = fcntl(i, F_GETFD);
      if (fdflags < 0) {
         km_info(KM_TRACE_EXEC, "fcntl() on guestfd %d failed, ignoring it, ", i);
         continue;
      }
      if ((fdflags & FD_CLOEXEC) != 0) {
         continue;
      }

      km_exec_fdtrace("before exec", i);

      // Build an entry for this open fd
      more_env_value = NULL;
      if (file->how == KM_FILE_HOW_EVENTFD) {   // event fd
         if (asprintf(&more_env_value, "{%x,%d,%x", KM_FDTYPE_EVENTFD, i, file->flags) == -1) {
            km_warn("failed save info for %s", file->name);
         }
         // append each event
         km_fs_event_t* event;
         TAILQ_FOREACH (event, &file->events, link) {
            char* tmp;
            if (asprintf(&tmp,
                     "%s,{%d,%x,%lx}%s",
                     more_env_value,
                     event->fd,
                     event->event.events,
                     event->event.data.u64,
                     event == TAILQ_LAST(&file->events, km_fs_event_head) ? "}" : "") == -1 ) {
                        km_warn("failed save info for %s eventfd %d", file->name, event->fd);
                     }
            free(more_env_value);
            more_env_value = tmp;
         }
      } else if (file->sockinfo == NULL) {   // non-socket fd
         struct stat st;
         if (fstat(i, &st) < 0) {
            free(env_value);
            return NULL;
         }
         if (S_ISFIFO(st.st_mode)) {
            if (asprintf(&more_env_value, "{%x,%d,%d,%x,%d}", KM_FDTYPE_PIPE, i, file->how, file->flags, file->ofd) == -1) {
               km_warn("failed save info for %s FIFO", file->name);
            }
         } else {
            km_file_ops_t* ops;
            km_fs_g2h_fd(i, &ops);
            if (asprintf(&more_env_value,
                     "{%x,%d,%d,%x,%d}",
                     KM_FDTYPE_FILE,
                     i,
                     file->how,
                     file->flags,
                     km_filename_table_line(ops))) {
                        km_warn("failed save info for non-socket %s", file->name);
                     }
         }
      } else if (file->how == KM_FILE_HOW_SOCKETPAIR0 ||
                 file->how == KM_FILE_HOW_SOCKETPAIR1) {   // socketpair
         if(asprintf(&more_env_value, "{%d,%d,%d,%d}", KM_FDTYPE_SOCKETPAIR, i, file->how, file->ofd) == -1) {
            km_warn("failed save info for socket pair %d %d", file->how, file->ofd);
         }
      } else {   // socket fd
         char asciihex[257];
         km_bin2hex((unsigned char*)file->sockinfo->addr, file->sockinfo->addrlen, asciihex);
         if (asprintf(&more_env_value,
                  "{%x,%d,%d,%d,%d,%d,%s}",
                  KM_FDTYPE_SOCKET,
                  i,
                  file->how,
                  file->sockinfo->state,
                  file->sockinfo->backlog,
                  file->ofd,
                  asciihex) == -1) {
                     km_warn("failed save info for socket %s", file->name);
                  }
      }

      // Paste info for this fd onto the end of what we have already accumulated.
      if (env_value == NULL) {
         env_value = more_env_value;
      } else {
         if (asprintf(&new_env_value, "%s,%s", env_value, more_env_value) == -1) {
            km_warn("failed save env info %s", more_env_value);
         }
         free(env_value);
         free(more_env_value);
         env_value = new_env_value;
      }
   }
   if (asprintf(&new_env_value, "%s=%s", varname, env_value != NULL ? env_value : "") == -1) {
      km_warn("failed save env var for %s", varname);
   }
   free(env_value);
   // The caller must free the returned pointer.
   return new_env_value;
}

/*
 * Free the memory associated with a guestfds entry we are discarding.
 */
static void km_fs_destroy_fd(int fd)
{
   km_file_t* file;
   km_fs_event_t* eventp;

   km_exec_get_file_pointer(fd, &file, NULL);

   while ((eventp = TAILQ_FIRST(&file->events)) != NULL) {
      TAILQ_REMOVE(&file->events, eventp, link);
      free(eventp);
   }

   free(file->sockinfo);
   file->sockinfo = NULL;
   free(file->name);
   file->name = NULL;

   file->inuse = 0;
}

static int km_exec_restore_file(int fd, int how, int flags, int index)
{
   km_file_t* file;

   km_exec_get_file_pointer(fd, &file, NULL);
   TAILQ_INIT(&file->events);

   assert(file->inuse == 0);
   file->inuse = 1;
   file->how = how;
   file->flags = flags;
   file->ofd = -1;
   file->ops = NULL;
   if (index >= 0) {
      file->ops = km_file_ops(index);
   }
   file->sockinfo = NULL;

   file->name = km_get_nonfile_name(fd);
   if (file->name == NULL) {
      return -1;
   }

   /*
    * snapshot depends on stdin, stdout, stderr being named [stdin] ....
    * to know when it should accept any /dev/pts/ rather than the one
    * that was open when the snapshot was taken.
    */
   if (fd == 0 && strncmp(file->name, "/dev/pts/", 9) == 0) {
      free(file->name);
      file->name = strdup(stdin_name);
   } else if (fd == 1 && strncmp(file->name, "/dev/pts/", 9) == 0) {
      free(file->name);
      file->name = strdup(stdout_name);
   } else if (fd == 2 && strncmp(file->name, "/dev/pts/", 9) == 0) {
      free(file->name);
      file->name = strdup(stderr_name);
   }
   return 0;
}

static int km_exec_restore_eventfd(int fd, int flags)
{
   km_file_t* file;
   km_exec_get_file_pointer(fd, &file, NULL);
   TAILQ_INIT(&file->events);

   assert(file->inuse == 0);
   file->inuse = 1;
   file->how = KM_FILE_HOW_EVENTFD;
   file->flags = flags;
   file->ofd = -1;
   file->ops = NULL;
   file->sockinfo = NULL;

   file->name = km_get_nonfile_name(fd);
   if (file->name == NULL) {
      return -1;
   }

   return 0;
}

static int km_exec_restore_pipe(int fd, int how, int flags, unsigned long ofd)
{
   km_file_t* file;
   km_exec_get_file_pointer(fd, &file, NULL);
   TAILQ_INIT(&file->events);

   file->inuse = 1;
   file->how = how;
   file->flags = flags;
   file->ofd = ofd;
   file->ops = NULL;
   file->sockinfo = NULL;

   file->name = km_get_nonfile_name(fd);
   if (file->name == NULL) {
      return -1;
   }

   return 0;
}

/*
 * Some socket properties can be queried with getsockopt().  For those, there is no
 * need for exec to propagate them to the child.  The child can just get them using
 * getsockopt().  Here we get all of those options and store them back into the
 * new sockinfo strucutre.
 */
static int km_exec_socket_get_sockopts(int fd)
{
   km_file_t* file;
   km_exec_get_file_pointer(fd, &file, NULL);
   int rc;
   int intbuf;
   socklen_t optlen;

   optlen = sizeof(intbuf);
   rc = getsockopt(fd, SOL_SOCKET, SO_TYPE, &intbuf, &optlen);
   if (rc < 0) {
      km_infox(KM_TRACE_EXEC, "getsockopt(%d, SO_TYPE ) failed, %d", fd, errno);
      return -errno;
   }
   file->sockinfo->type = intbuf;
   optlen = sizeof(intbuf);
   rc = getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &intbuf, &optlen);
   if (rc < 0) {
      km_infox(KM_TRACE_EXEC, "getsockopt(%d, SO_DOMAIN ) failed, %d", fd, errno);
      return -errno;
   }
   file->sockinfo->domain = intbuf;
   optlen = sizeof(intbuf);
   rc = getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &intbuf, &optlen);
   if (rc < 0) {
      km_infox(KM_TRACE_EXEC, "getsockopt(%d, SO_PROTOCOL ) failed, %d", fd, errno);
      return -errno;
   }
   file->sockinfo->protocol = intbuf;
   return 0;
}

static int km_exec_restore_socketpair(int fd, int how, int ofd)
{
   km_file_t* file;
   km_exec_get_file_pointer(fd, &file, NULL);
   TAILQ_INIT(&file->events);

   file->inuse = 1;
   file->how = how;
   file->flags = 0;
   file->ofd = ofd;
   file->ops = NULL;

   file->name = km_get_nonfile_name(fd);
   if (file->name == NULL) {
      km_fs_destroy_fd(fd);
      return -1;
   }

   file->sockinfo = malloc(sizeof(km_fd_socket_t));
   if (file->sockinfo == NULL) {
      km_fs_destroy_fd(fd);
      return -1;
   }
   if (km_exec_socket_get_sockopts(fd) != 0) {
      km_fs_destroy_fd(fd);
      return -1;
   }
   file->sockinfo->state = KM_SOCK_STATE_CONNECT;
   file->sockinfo->backlog = 0;
   file->sockinfo->addrlen = 0;

   return 0;
}

static int
km_exec_restore_socket(int fd, int how, int state, int backlog, int ofd, km_fd_socket_t* sockinfo)
{
   km_file_t* file;
   km_exec_get_file_pointer(fd, &file, NULL);
   TAILQ_INIT(&file->events);

   file->inuse = 1;
   file->how = how;
   file->flags = 0;
   file->ofd = ofd;
   file->ops = NULL;
   file->sockinfo = sockinfo;

   file->name = km_get_nonfile_name(fd);
   if (file->name == NULL) {
      km_fs_destroy_fd(fd);
      return -1;
   }

   if (km_exec_socket_get_sockopts(fd) != 0) {
      km_fs_destroy_fd(fd);
      return -1;
   }
   file->sockinfo->state = state;
   file->sockinfo->backlog = backlog;

   return 0;
}

/*
 * Parse a string produced by km_exec_save_fd() and reconstruct the km payload file table.
 */
int km_exec_restore_fd(char* env_value)
{
   char* p = env_value;
   char* q;
   int fd = 0;
   int how;
   int flags;
   int index;
   int ofd;
   int backlog;
   int state;

   while (*p != 0) {
      int tag;
      km_infox(KM_TRACE_EXEC, "Processing: %s", p);
      // Scan the first number to figure out what the rest of it looks like.
      if (sscanf(p, "{%x,", &tag) != 1) {
         km_infox(KM_TRACE_EXEC, "Unable to get fd type tag from %s", p);
         return -1;
      }
      q = strchr(p, ',');
      if (q == NULL) {
         return -1;
      }
      q++;
      switch (tag) {
         case KM_FDTYPE_FILE:
            // {0,0,0,0,-1}
            if (sscanf(q, "%d,%d,%x,%d}", &fd, &how, &flags, &index) != 4) {
               return -1;
            }
            km_exec_restore_file(fd, how, flags, index);
            break;

         case KM_FDTYPE_PIPE:
            // {1,9,2,10600,8}
            if (sscanf(q, "%d,%d,%x,%d}", &fd, &how, &flags, &ofd) != 4) {
               return -1;
            }
            km_exec_restore_pipe(fd, how, flags, ofd);
            break;

         case KM_FDTYPE_SOCKETPAIR:
            // {2,6,6,7}
            if (sscanf(q, "%d,%d,%d}", &fd, &how, &ofd) != 3) {
               return -1;
            }
            km_exec_restore_socketpair(fd, how, ofd);
            break;

         case KM_FDTYPE_SOCKET:
            // {3,5,4,2,5,-1,020056cf000000000000000000000000},
            if (sscanf(q, "%d,%d,%d,%d,%d,", &fd, &how, &state, &backlog, &ofd) != 5) {
               return -1;
            }
            km_fd_socket_t* sockinfo = calloc(1, sizeof(km_fd_socket_t));
            if (sockinfo == NULL) {
               return -1;
            }
            char* saveq = q;
            q = strchr(q, '}');
            if (q == NULL) {
               free(sockinfo);
               return -1;
            }
            while (*(q - 1) != ',') {
               q--;
               if (q == saveq) {
                  // We should have found a ',' by now.  We got this far because sscanf() above found one.
                  km_infox(KM_TRACE_EXEC, "Didn't find a comma in %s?", saveq);
                  break;
               }
            }
            km_hex2bin(q, (unsigned char*)sockinfo->addr, &sockinfo->addrlen);
            if (km_exec_restore_socket(fd, how, state, backlog, ofd, sockinfo) < 0) {
               // km_exec_restore_socket() will free sockinfo when it fails.
               km_infox(KM_TRACE_EXEC, "Unable to restore socket fd %d", fd);
               return -1;
            }
            break;

         case KM_FDTYPE_EVENTFD:
            // The description of an epollfd looks like this, there can be 0 or more events.
            // {4,10,0,{4,1,7fff00000004},{6,4,7fff00000006}}
            if (sscanf(q, "%d,%x", &fd, &flags) != 2) {
               km_infox(KM_TRACE_EXEC, "Didn't find expected delimiter");
               return -1;
            }
            q = strpbrk(q, "{}");
            if (q == NULL) {
               km_infox(KM_TRACE_EXEC, "Didn't find expected delimiter");
               return -1;
            }
            km_exec_restore_eventfd(fd, flags);
            km_file_t* file;
            km_exec_get_file_pointer(fd, &file, NULL);
            if (*q == '{') {
               q--;
            }
            while (*q == ',') {
               uint32_t events;
               uint64_t data;
               q++;
               km_infox(KM_TRACE_EXEC, "Processing event: %s", q);
               if (sscanf(q, "{%d,%x,%lx}", &fd, &events, &data) != 3) {
                  km_fs_destroy_fd(fd);
                  return -1;
               }
               q = strchr(q, '}');
               if (q == NULL) {
                  km_fs_destroy_fd(fd);
                  return -1;
               }
               q++;
               km_fs_event_t* eventp;
               eventp = malloc(sizeof(km_fs_event_t));
               if (eventp == NULL) {
                  km_fs_destroy_fd(fd);
                  return -1;
               }
               eventp->fd = fd;
               eventp->event.events = events;
               eventp->event.data.u64 = data;
               TAILQ_INSERT_TAIL(&file->events, eventp, link);
            }
            if (*q != '}') {
               return -1;
            }
            // events are complicated enough that we must advance p beyond this entry
            p = q;
            break;

         default:
            km_infox(KM_TRACE_EXEC, "Unknown fd tag 0x%x", tag);
            return -1;
            break;
      }
      q = strchr(p, '}');
      if (q == NULL) {
         return -1;
      }
      p = q + 1;
      if (*p == ',') {
         p++;
      }
   }
   return 0;
}
