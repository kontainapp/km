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
 * Stubs for some of the _chk_ family of functions. They check nothing, just call underlying
 * non-checking implementations
 */

#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/select.h>

#include "musl/include/setjmp.h"
#include "musl/include/sys/param.h"
#include "syscall.h"

void __chk_fail(void) __attribute__((__noreturn__));
void __chk_fail(void)
{
   abort();
}

int __fprintf_chk(FILE* f, __attribute__((unused)) int flag, const char* format, ...)
{
   va_list arg;
   int rc;

   va_start(arg, format);
   rc = vfprintf(f, format, arg);
   va_end(arg);

   return rc;
}

int __fwprintf_chk(FILE* f, __attribute__((unused)) int flag, const char* format, ...)
{
   va_list arg;
   int rc;

   va_start(arg, format);
   rc = vfprintf(f, format, arg);
   va_end(arg);

   return rc;
}

int __printf_chk(__attribute__((unused)) int flag, const char* format, ...)
{
   va_list arg;
   int rc;

   va_start(arg, format);
   rc = vprintf(format, arg);
   va_end(arg);

   return rc;
}

int __sprintf_chk(char* s,
                  __attribute__((unused)) int flags,
                  __attribute__((unused)) size_t slen,
                  const char* format,
                  ...)
{
   va_list arg;
   int rc;

   va_start(arg, format);
   rc = vsprintf(s, format, arg);
   va_end(arg);

   return rc;
}

extern int __snprintf_chk(char* s,
                          size_t n,
                          __attribute__((unused)) int flag,
                          __attribute__((unused)) size_t slen,
                          const char* format,
                          ...)
{
   va_list arg;
   int rc;

   va_start(arg, format);
   rc = vsnprintf(s, n, format, arg);
   va_end(arg);

   return rc;
}

int __vfprintf_chk(FILE* fp, __attribute__((unused)) int flag, const char* format, va_list arg)
{
   return vfprintf(fp, format, arg);
}

int __vprintf_chk(__attribute__((unused)) int flag, const char* format, va_list arg)
{
   return vprintf(format, arg);
}

int __vsnprintf_chk(char* s,
                    size_t maxlen,
                    __attribute__((unused)) int flag,
                    __attribute__((unused)) size_t slen,
                    const char* format,
                    va_list arg)
{
   return vsnprintf(s, maxlen, format, arg);
}

int __vsprintf_chk(char* s,
                   __attribute__((unused)) int flag,
                   __attribute__((unused)) size_t slen,
                   const char* format,
                   va_list arg)
{
   return vsprintf(s, format, arg);
}

char* __strncat_chk(char* s1, const char* s2, size_t n, __attribute__((unused)) size_t s1len)
{
   return strncat(s1, s2, n);
}

// isnan is a macro, so we cannot use alias mechanism here
int __isnan(double arg)
{
   return isnan(arg);
}

void __syslog_chk(int priority, __attribute__((unused)) int flag, const char* format)
{
   syslog(priority, format);
}

void __longjmp_chk(jmp_buf env, int val)
{
   longjmp(env, val);
}

void* __memset_chk(void* __dest, int __ch, size_t __len, size_t __resid)
{
   return __builtin_memset(__dest, __ch, __len);
}

void* __memcpy_chk(void* __restrict __dest, const void* __restrict __src, size_t __len, size_t __resid)
{
   return __builtin_memcpy(__dest, __src, __len);
}

char* __strcat_chk(char* __restrict __dest, const char* __restrict __src, size_t __resid)
{
   return __builtin_strcat(__dest, __src);
}

void __explicit_bzero_chk(void* dst, size_t len, size_t dstlen)
{
   assert(dstlen >= len);
   memset(dst, '\0', len);
   /* Compiler barrier.  */
   asm volatile("" ::: "memory");
}

int __poll_chk(struct pollfd* fds, nfds_t nfds, int timeout, __SIZE_TYPE__ fdslen)
{
   if (fdslen / sizeof(*fds) < nfds) {
      __chk_fail();
   }

   return poll(fds, nfds, timeout);
}

int __asprintf_chk(char** result_ptr, __attribute__((unused)) int flags, const char* format, va_list args)
{
   return asprintf(result_ptr, format, args);
}

int __vasprintf_chk(char** result_ptr, __attribute__((unused)) int flags, const char* format, va_list args)
{
   return asprintf(result_ptr, format, args);
}

char* __strncpy_chk(char* s1, const char* s2, size_t n, size_t s1_len)
{
   if (__builtin_expect(s1_len < n, 0)) {
      __chk_fail();
   }
   return strncpy(s1, s2, n);
}

/* Copy src to dest, returning the address of the terminating '\0' in dest.  */
char* __stpcpy_chk(char* dest, const char* src, size_t dest_len)
{
   size_t len;

   if ((len = strlen(src)) >= dest_len) {
      __chk_fail();
   }
   return (char*)memcpy(dest, src, len + 1) + len;
}

/* Note: Python on Ubuntu also wants  __wcscat_chk */

char* __realpath_chk(const char* buf, char* resolved, size_t resolvedlen)
{
   if (resolvedlen < MAXPATHLEN) {
      __chk_fail();
   }
   return realpath(buf, resolved);
}

long int __fdelt_chk(long int d)
{
   if (d < 0 || d >= FD_SETSIZE) {
      __chk_fail();
   }
   return d / NFDBITS;
}

wchar_t* __wcsncpy_chk(wchar_t* dest, const wchar_t* src, size_t n, size_t destlen)
{
   if (destlen < n) {
      __chk_fail();
   }
   return wcsncpy(dest, src, n);
}

wchar_t* __wcscpy_chk(wchar_t* dest, const wchar_t* src, size_t n)
{
   return wcscpy(dest, src);
}

char* __strcpy_chk(char* dest, const char* src, size_t destlen)
{
   size_t len = strlen(src);
   if (len >= destlen) {
      __chk_fail();
   }
   return memcpy(dest, src, len + 1);
}

size_t __fread_chk(void* __restrict ptr, size_t ptrlen, size_t size, size_t n, FILE* __restrict stream)
{
   return fread(ptr, ptrlen, size, stream);
}

ssize_t __read_chk(int fd, void* buf, size_t nbytes, size_t buflen)
{
   if (nbytes > buflen) {
      __chk_fail();
   }
   return read(fd, buf, nbytes);
}

ssize_t __pread64_chk(int fd, void* buf, size_t nbytes, off64_t offset, size_t buflen)
{
   if (nbytes > buflen) {
      __chk_fail();
   }
   return pread64(fd, buf, nbytes, offset);
}

void* __memmove_chk(void* dest, const void* src, size_t len, size_t dstlen)
{
   if (__builtin_expect(dstlen < len, 0)) {
      __chk_fail();
   }
   return memmove(dest, src, len);
}

static mbstate_t internal;   // internal shift state, gets used if ps==NULL
size_t __mbrlen(const char* s, size_t n, mbstate_t* ps)
{
   return mbrtowc(NULL, s, n, ps ?: &internal);
}
