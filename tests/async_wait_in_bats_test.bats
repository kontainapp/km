#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# A simple test to validate bats bug in 'run wait' which returns 255 instead of 124 on timeout.
# To run:
#   ./bats/bin/bats ./async_wait_in_bats_test.bats
# Expect:
# 3 tests, 1 failure (due to the bats bug, the last one should fail with
# "expected : 124 actual   : 255"

load 'bats-support/load'
load 'bats-assert/load'

timeout=0.5s
doit()
{
   echo timeout --foreground --signal=SIGABRT $timeout "$@"
   timeout --foreground --signal=SIGABRT $timeout "$@"
}

# A workaround for 'run wait' returning 255 for any failure.
# For commands NOT using 'run' but put in the background, use this function to wait and
# check for expected eror code (or 0, if expecting success)
# e.g.
#     wait_and_check 124 - wait for the %1 background job
wait_and_check()
{
   s=0; wait %1 || s=$? ; assert_equal $s $1
}


@test "simple" {
   # no timeout
   doit sleep 0.1s &
   ls m*c; sleep 0.2s # some stuff
   wait_and_check 0

   # timeout
   doit sleep 1s &
   ls m*c; sleep 0.2s # some stuff
   wait_and_check 124
}

@test "exits with no timeout - correct results" {
   name=/tmp/bats_wait_test_$$; cat <<EOF > $name.c
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
int main(int argc, char** argv) {
   if (argc > 1) {
      if (strcmp(argv[1], "abort") == 0) {
         printf("calling abort\n");
         abort();
      } else if (strcmp(argv[1], "exit") == 0) {
         printf("calling err 12\n");
         errx(12," errx 12");
      } else {
         printf("return ok");
         exit(0);
      }
   }
   printf("exiting with 5\n");
   exit(5);
}
EOF
   cc -o $name $name.c

   doit $name abort &
   wait_and_check 134

   doit $name exit &
   wait_and_check 12

   doit $name ok &
   wait_and_check 0

   rm $name $name.c
}

@test "test should fail. bug in 'run wait' - 255 instead of 124" {
   doit sleep 10s &
   run wait %1
   assert_failure 124
}

