/*
 * Copyright 2023 Kontain Inc
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
 * Test some DNS library functions.
 */

#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "greatest/greatest.h"

TEST test_huggingface()
{
   struct addrinfo* res;
   int rc = getaddrinfo("huggingface.co", "443", NULL, &res);

   if (rc != 0) {
      fprintf(stderr, "getaddinfo returned error %d(%s)\n", rc, gai_strerror(rc));
      ASSERT(0);
   }
   PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char* argv[])
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(test_huggingface);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
