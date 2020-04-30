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

extern char** km_exec_build_env(char** envp);
extern char** km_exec_build_argv(char* filename, char** argv);
extern int km_exec_recover_kmstate(void);
extern int km_exec_recover_guestfd(void);
extern void km_exec_init(int argc, char** argv);
extern void km_exec_fini(void);

#endif // !defined(__KM_EXEC_H__)
