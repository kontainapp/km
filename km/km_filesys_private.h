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
#ifndef __KM_FILESYS_PRIVATE_H__
#define __KM_FILESYS_PRIVATE_H__

/*
 * Definitions of km_filesys.c private structures that have been exposed because km_filesys.c is
 * getting big and we need to split out functionality to another file.
 */

// Each km_file_t has an optional point to socket state described by this structure.
typedef struct km_fd_socket {
   int state;
   int backlog;
   int domain;
   int type;
   int protocol;
   // currently all linux sockaddr variations fit in 128 bytes
   int addrlen;
   char addr[128];   // the local address passed to bind()
} km_fd_socket_t;

// Valid values for the state field in km_fd_socket_t
#define KM_SOCK_STATE_OPEN 0
#define KM_SOCK_STATE_BIND 1
#define KM_SOCK_STATE_LISTEN 2
#define KM_SOCK_STATE_ACCEPT 3
#define KM_SOCK_STATE_CONNECT 4

// Description of an event associated with an eventfd.
typedef struct km_fs_event {
   TAILQ_ENTRY(km_fs_event) link;
   int fd;
   struct epoll_event event;
} km_fs_event_t;

// Each file opened by the guest has one of these structures.
typedef struct km_file {
   int inuse;            // if true, this entry is inuse.
   int how;              // How was this file created
   int flags;            // Open flags
   int error;            // If non-zero, error code to return for all syscalls. Snapshot recovery.
   km_file_ops_t* ops;   // Overwritten file ops for file matched at open
   int ofd;              // 'other' fd (pipe and socketpair)
   char* name;           // the name opened to yield the guest fd
   km_fd_socket_t* sockinfo;                           // For sockets
   TAILQ_HEAD(km_fs_event_head, km_fs_event) events;   // for epoll_create fd's
} km_file_t;

// Valid values for the how field in km_file_t
#define KM_FILE_HOW_OPEN 0    /* Regular open */
#define KM_FILE_HOW_PIPE_0 1  /* read half of pipe */
#define KM_FILE_HOW_PIPE_1 2  /* write half of pipe */
#define KM_FILE_HOW_EPOLLFD 3 /* epoll_create() */
#define KM_FILE_HOW_SOCKET 4
#define KM_FILE_HOW_ACCEPT 5
#define KM_FILE_HOW_SOCKETPAIR0 6
#define KM_FILE_HOW_SOCKETPAIR1 7
#define KM_FILE_HOW_RECVMSG 8
#define KM_FILE_HOW_EVENTFD 9 /* eventfd() */

// machine.filesys points to a km_filesys_t structure.
typedef struct km_filesys {
   int nfdmap;               // size of file descriptor maps
   km_file_t* guest_files;   // Indexed by guestfd
} km_filesys_t;

static const_string_t stdin_name = "[stdin]";
static const_string_t stdout_name = "[stdout]";
static const_string_t stderr_name = "[stderr]";

static inline km_filesys_t* km_fs()
{
   return machine.filesys;
}

int km_is_file_used(km_file_t *file);
void km_set_file_used(km_file_t *file, int val);

#endif   // !defined(__KM_FILESYS_PRIVATE_H__)
