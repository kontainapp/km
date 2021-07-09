/*
 * Copyright 2021 Kontain Inc.
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

#ifndef __KM_EXEC_H__
#define __KM_EXEC_H__

#include <sys/epoll.h>
#include <sys/socket.h>
#include "km_filesys.h"
#include "km_filesys_private.h"

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

// Helpers. Here because we do not have km_helpers.h
int km_is_shell_path(const char*);
int km_is_env_path(const char*);

#endif   // !defined(__KM_EXEC_H__)
