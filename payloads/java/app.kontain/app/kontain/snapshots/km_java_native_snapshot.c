/*
 * Copyright 2021 Kontain Inc.
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
