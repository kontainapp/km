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

#include <stdlib.h>

unsigned long long strtoll_l(const char* nptr, char** endptr, size_t base, void* unused)
{
   return strtoll(nptr, endptr, base);
}

unsigned long long strtoull_l(const char* nptr, char** endptr, size_t base, void* unused)
{
   return strtoull(nptr, endptr, base);
}

double __strtof_internal(const char* nptr, char** endptr, int group, void* unused)
{
   return strtof(nptr, endptr);
}

double __strtod_internal(const char* nptr, char** endptr, int group, void* unused)
{
   return strtod(nptr, endptr);
}
