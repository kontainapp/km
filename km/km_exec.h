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

#ifndef __KM_EXEC_H__
#define __KM_EXEC_H__

static const_string_t KMPATH = "KMPATH";
static const_string_t SHELL_PATH = "/bin/sh";

extern char** km_exec_payload_env;

extern char** km_exec_build_env(char** envp);
extern char** km_exec_build_argv(char* filename, char** argv);
extern int km_exec_recover_kmstate(void);
extern int km_exec_recover_guestfd(void);
extern void km_exec_init(int argc, char** argv);
extern void km_exec_fini(void);
extern char* km_get_payload_name(char* payload_file, char** extra_arg);

#endif   // !defined(__KM_EXEC_H__)
