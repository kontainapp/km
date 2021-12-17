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

#ifndef __KM_EXEC_FD_SAVE_RECOVER_H__
#define __KM_EXEC_FD_SAVE_RECOVER_H__

char* km_exec_save_fd(char* varname);
int km_exec_restore_fd(char* env_value);
void km_exec_fdtrace(char* tag, int fd);

#endif   // !defined(__KM_EXEC_FD_SAVE_RECOVER_H__)
