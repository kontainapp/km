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
 * KM Management/Control Plane
 */

#ifndef KM_MANAGEMENT_H_
#define KM_MANAGEMENT_H_

#include "km.h"

void km_mgt_init(char* path);
void km_mgt_fini();
#endif