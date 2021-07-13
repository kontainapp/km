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

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <syscall.h>
#include <sys/socket.h>
#include "km_hcalls.h"

// stub that return an error to the payload
int __dummy_stub(void)
{
   errno = ENOSYS;
   return -1;
}
#define __stub__(_func_) int _func_() __attribute__((alias("__dummy_stub")))

// stub that blindly mimics success but does nothing
int __dummy_ok_stub(void)
{
   return 0;
}
#define __ok_stub__(_func_) int _func_() __attribute__((alias("__dummy_ok_stub")))

// stub that stops payload
static void dump_core_stub(const char* syscall_name)
{
   fprintf(stderr, "runtime_km: call to unsupported `%s', generating core dump\n", syscall_name);
   syscall(SYS_exit, -1);
}
#define __stub_core__(_func_)                                                                      \
   hidden void __dump_core_stub_##_func_(void) { dump_core_stub(#_func_); }                        \
   void _func_() __attribute__((alias("__dump_core_stub_" #_func_)))

#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

/*
 * Stubs to allow linking but not executing the functionality explicitly removed from libruntime.
 * Guest code still can call syscall directly and trigger KM unimplemented hypercall.
 */
__stub__(getcontext);
__stub__(setcontext);
__stub__(makecontext);
__stub__(swapcontext);
__stub_core__(__sched_cpualloc);
__stub_core__(__sched_cpufree);
__ok_stub__(__register_atfork);   // TODO: may need to implement when doing fork() and dlopen
__stub__(shmget);
__stub__(shmat);
__stub__(shmdt);
__stub__(shmctl);

__stub__(backtrace);
__stub__(backtrace_symbols_fd);
__stub__(mallopt);
__stub__(mallinfo);
__stub__(pthread_getname_np);

// TODO: Implement these
__stub_core__(jnl);
__stub_core__(ynl);
__stub__(register_printf_specifier);
__stub__(register_printf_modifier);
__stub__(register_printf_type);

void* dlvsym(void* handle, char* symbol, char* version)
{
   return dlsym(handle, symbol);
}

/*
 * executable stubs, returning simple canned value
 */
const char* gnu_get_libc_version(void)
{
   return "km";
}

// Files compiled with libc sys/sysmacros.h will refer to functions below (#define-d in musl sys/sysmacros.h)
const int gnu_dev_major(int x)
{
   return ((unsigned)((((x) >> 31 >> 1) & 0xfffff000) | (((x) >> 8) & 0x00000fff)));
}

const int gnu_dev_minor(int x)
{
   return ((unsigned)((((x) >> 12) & 0xffffff00) | ((x)&0x000000ff)));
}

const int gnu_dev_makedev(int x, int y)
{
   return ((((x)&0xfffff000ULL) << 32) | (((x)&0x00000fffULL) << 8) | (((y)&0xffffff00ULL) << 12) |
           (((y)&0x000000ffULL)));
}

char** backtrace_symbols(void* const* buffer, int size)
{
   return NULL;
}

struct cmsghdr* __cmsg_nxthdr(struct msghdr* msgh, struct cmsghdr* cmsg)
{
   return CMSG_NXTHDR(msgh, cmsg);
}
