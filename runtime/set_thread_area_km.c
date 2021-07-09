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

#include "km_hcalls.h"
#include "syscall.h"

#ifndef ARCH_SET_FS
#define ARCH_SET_FS 0x1002
#endif

uint64_t __set_thread_area(uint64_t addr)
{
   return syscall(SYS_arch_prctl, ARCH_SET_FS, addr);
}
