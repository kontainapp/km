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

#ifndef __KM_EXEC_FD_SAVE_RECOVER_H__
#define __KM_EXEC_FD_SAVE_RECOVER_H__

char* km_exec_save_fd(char* varname);
int km_exec_restore_fd(char* env_value);
void km_exec_fdtrace(char* tag, int fd);

#endif // !defined(__KM_EXEC_FD_SAVE_RECOVER_H__)
