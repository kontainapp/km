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

/*
 * exec() system call state being passed back to the km main thread
 * for it to handle program load and start.
 */
typedef struct {
   uint8_t exec_in_progress;    // if true an execve() system call is in progress.
   uint8_t* argp_envp;          // memory area that contains argp and envp
   int argc;                    // number of elements in argp[]
   char** argp;                 // points into argp_envp
   int envc;                    // number of elements in envp[]
   char** envp;                 // points into argp_envp
   char filename[MAXPATHLEN];   // for execve() the path of executable to run
   int fd;                      // for execveat() the open fd of the executable
} km_exec_state_t;

extern km_exec_state_t km_exec_state;

extern int km_copy_argvenv_to_km(char* argv[], char* envp[]);
extern void km_reinit_for_exec(void);

#endif // !defined(__KM_EXEC_H__)
