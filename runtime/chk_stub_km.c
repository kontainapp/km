/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Stubs for some of the _chk_ family of functions. They check nothing, just call underlying
 * non-checking implementations
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "musl/include/setjmp.h"

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

extern int __snprintf_chk (char *s, size_t n, __attribute__((unused)) int flag,
                           __attribute__((unused)) size_t slen, const char *format,
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

void __longjmp_chk(jmp_buf env, int val)
{
   longjmp(env, val);
}

/* Python on Ubuntu wants these:
__fdelt_chk
__memcpy_chk
__memmove_chk
__open64_2
__realpath_chk
__snprintf_chk
__strcpy_chk
__strncpy_chk
__wcscat_chk
__wcscpy_chk
__wcsncpy_chk
*/
