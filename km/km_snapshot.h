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

/*
 * Kontain VM Snapshot/Restart.
 */

#ifndef KM_SNAPSHOT_H_
#define KM_SNAPSHOT_H_

#include "km.h"
#include "km_elf.h"

void km_set_snapshot_path(char* path);
char* km_get_snapshot_path();
int km_snapshot_create(km_vcpu_t* vcpu, char* label, char* path, int live);
int km_snapshot_restore(km_elf_t* elf);
int km_snapshot_notes_apply(char* notebuf, size_t notesize, int type, int (*func)(char*, size_t));

void km_set_snapshot_input_path(char* path);
char* km_get_snapshot_input_path();
void km_set_snapshot_output_path(char* path);
char* km_get_snapshot_output_path();

int km_snapshot_getdata(km_vcpu_t* vcpu, char* buf, int buflen);
int km_snapshot_putdata(km_vcpu_t* vcpu, char* buf, int buflen);

#define KM_TRACE_SNAPSHOT "snapshot"

#endif
