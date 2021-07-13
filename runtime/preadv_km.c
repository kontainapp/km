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

#include <unistd.h>
#include <sys/uio.h>
#include "syscall.h"

ssize_t preadv(int fd, const struct iovec* iov, int count, off_t ofs)
{
   return syscall_cp(SYS_preadv, fd, iov, count, (long)(ofs), (long)(ofs >> 32));
}

weak_alias(preadv, preadv64);
weak_alias(preadv, preadv64v2);
