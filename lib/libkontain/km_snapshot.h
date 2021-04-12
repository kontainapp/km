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

#ifndef __KM_SNAPSHOT_H__
#define __KM_SNAPSHOT_H__

int snapshot(char* label, char* application_name, int snapshot_live);
size_t snapshot_getdata(void* buffer, size_t count);
size_t snapshot_putdata(void* buffer, size_t count);

#endif /* #ifndef __KM_SNAPSHOT_H__ */
