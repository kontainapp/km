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

#ifndef __KM_EXEC_H__
#define __KM_EXEC_H__

#include <sys/socket.h>
#include <sys/epoll.h>
#include "km_filesys.h"
#include "km_filesys_private.h"

static const_string_t KMPATH = "KMPATH";
static const_string_t SHELL_PATH = "/bin/sh";
static const_string_t PAYLOAD_SUFFIX = ".km";

void km_exec_get_file_pointer(int fd, km_file_t** filep, int* nfds);
char** km_exec_build_env(char** envp);
char** km_exec_build_argv(char* filename, char** argv, char** envp);
int km_exec_recover_kmstate(void);
int km_exec_recover_guestfd(void);
void km_exec_init_args(int argc, char** argv);
void km_exec_fini(void);
int km_called_via_exec(void);
pid_t km_exec_pid(void);
pid_t km_exec_ppid(void);
pid_t km_exec_next_pid(void);

#endif   // !defined(__KM_EXEC_H__)
