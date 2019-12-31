#!/bin/bash
# use this if started as a standalone script:
if [ -z "$BATS_TEST_FILENAME" ] ; then "exec" "`dirname $0`/bats/bin/bats" "$0" "$@" ; fi

# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.
#
# BATS (BASH Test Suite) definition for KM core test pass
#
# See ./bats/... for docs
#
cd $BATS_ROOT/.. # bats sits under tests, so this will move us to tests
load 'bats-support/load' # see manual in bats-support/README.md
load 'bats-assert/load'  # see manual in bats-assert/README.mnd

# KM binary location.
if [ -z "$KM_BIN" ] ; then
   echo "Please make sure KM_BIN env is defined and points to KM executable." >&3
   exit 10
fi

if [ -z "$KM_LDSO" ] ; then
   KM_LDSO="$(git rev-parse --show-toplevel)/build/runtime/libc.so"
fi

if [ -z "$KM_LDSO_PATH" ] ; then
   KM_LDSO_PATH="/opt/kontain/lib64:/lib64"
fi

if [ -z "$TIME_INFO" ] ; then
   echo "Please make sure TIME_INFO env is defined and points to a file. We will put detailed timing info there">&3
   exit 10
fi

if [ -z "$BRANCH" ] ; then
   BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

# we will kill any test if takes longer
timeout=150

# this is how we invoke KM - with a timeout and reporting run time
function km_with_timeout () {
   /usr/bin/time -f "elapsed %E user %U system %S mem %M KiB (km $*) " -a -o $TIME_INFO \
      timeout --foreground $timeout \
         ${KM_BIN} --dynlinker=${KM_LDSO} ${KM_ARGS} "$@"
   s=$?; if [ $s -eq 124 ] ; then echo "\nTimed out in $timeout" ; fi ; return $s
}

gdb_hbreak_alias="$(if ! grep 'vendor_id' /proc/cpuinfo  | grep -iq intel ; \
   then echo alias mybreak=hbreak ; else echo alias mybreak=break  ; \
fi )"
# this is how we invoke gdb - with timeout
function gdb_with_timeout () {
   timeout --foreground $timeout \
      gdb -ex="$gdb_hbreak_alias" "$@"
   s=$?; if [ $s -eq 124 ] ; then echo "\nTimed out in $timeout" ; fi ; return $s
}


# this is needed for running in Docker - bats uses 'tput' so it needs the TERM
TERM=xterm

bus_width() {
   #  use KM to print out physical memory width on the test machine
   echo $(${KM} -V exit_value_test.km 2>& 1 | awk '/physical memory width/ {print $6;}')
}

# Teardown for each test. Note that printing to stdout/stderr in this function
# only shows up on errors. For print on success too, redirect to >&3
teardown() {
      echo -e "\nkm output:\n${output}"
}

# Check if running in docker.
function in_docker() {
  cat /proc/1/cgroup | grep -q docker
}

