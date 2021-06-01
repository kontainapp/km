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

#include "app_kontain_snapshots_Snapshot.h"
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

/*
 * Take a snapshot of the current process.
 */
JNIEXPORT jint JNICALL Java_app_kontain_snapshots_Snapshot_take_1native
  (JNIEnv * env, jobject thisObj, jstring label, jstring description)
{
   struct utsname ut;
   if (uname(&ut) < 0) {
      return -1;
   }
   /*
    * 'kontain' is set in km/km_hcalls.c. If the value there changes,
    * the value here needs to change with it.
    */
   if (strstr(ut.release, "kontain") == NULL) {
      return -1;
   }
   // 505 - KM Hypercall
   jboolean iscopy;
   const char* label_str = (*env)->GetStringUTFChars(env, label, &iscopy);
   const char* desc_str = (*env)->GetStringUTFChars(env, description, &iscopy);
   return syscall(505, label_str, desc_str);
}
