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
 *
 * Kontain VM Snapshot/Restart.
 */

#ifndef KM_SNAPSHOT_H_
#define KM_SNAPSHOT_H_

#include "km.h"
#include "km_elf.h"

void km_set_snapshot_path(char* path);
char* km_get_snapshot_path();
int km_snapshot_create(km_vcpu_t* vcpu, char *label, char *path, int live);
int km_snapshot_restore(const char* file);
int km_snapshot_notes_apply(char* notebuf, size_t notesize, int type, int (*func)(char*, size_t));

#define KM_TRACE_SNAPSHOT "snapshot"

#endif