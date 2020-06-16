#!/bin/bash
# use this if started as a standalone script:
if [ -z "$BATS_TEST_FILENAME" ] ; then "exec" "`dirname $0`/bats/bin/bats" "$0" "$@" ; fi

# Copyright © 2019-2020 Kontain Inc. All rights reserved.
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
# See ./bats/... for docs on bats, bats-support and bats-assert functionality:
#  - ./bats*/README.md and man/ files)
#  - The same docs is available on github in bats-core bats-assist and bats-support repositories
#

cd $BATS_ROOT/.. # bats sits under tests, so this will move us to tests
load 'bats-support/load' # see manual in bats-support/README.md
load 'bats-assert/load'  # see manual in bats-assert/README.mnd

# KM binary location.
if [ -z "$KM_BIN" ] ; then
   echo "Please make sure KM_BIN env is defined and points to KM executable." >&3
   exit 10
fi

# virtualization type
if [ -z "$USE_VIRT" ] ; then
   echo "Please make sure USE_VIRT env is defined." >&3
   exit 10
fi

PREFIX=/opt/kontain
RT=${PREFIX}/runtime
LC=${PREFIX}/alpine-lib/usr/lib

# Note: the /opt/kontain/runtime/libc.so is hardcoded in GDB support, so when using
# from other locations some shared lib GDB files location may break
if [ -z "$KM_LDSO" ] ; then
   KM_LDSO=${RT}/libc.so
fi

if [ -z "$KM_LDSO_PATH" ] ; then
   KM_LDSO_PATH=${RT}:${LC}
fi

if [ -z "$TIME_INFO" ] ; then
   echo "Please make sure TIME_INFO env is defined and points to a file. We will put detailed timing info there">&3
   exit 10
fi

if [ -z "$BRANCH" ] ; then
   BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

# Now find out what test type we were asked to run ('static' is default)
# and set appropriate extension and km args
test_type=${KM_TEST_TYPE:-static}

case $test_type in
   static)
      ext=.km
      ;;
   dynamic)
      ext=.kmd
      ;;
   native_static)
      ext=.alpine.km
      ;;
   native_dynamic)
      ext=.alpine.kmd
      ;;
   so)
      ext=.km.so
      KM_ARGS="${KM_ARGS} ${KM_LDSO} --library-path=${KM_LDSO_PATH}"
      ;;
   *)
      echo "Unknown test type: $test_type, should be 'static', 'dynamic', 'native_static', 'native_dynamic' or 'so'"
      export KM_BIN=fail
      ;;
esac

# USE_VIRT is exported by run_bats_tests.sh
if [ "${USE_VIRT}" = 'kvm' ] ; then
   KM_ARGS="--use-kvm $KM_ARGS"
elif [ "${USE_VIRT}" = 'kkm' ] ; then
   KM_ARGS="--use-kkm $KM_ARGS"
else
   echo "Unknown virtualization type : ${USE_VIRT}"
   exit 10
fi

# we will kill any test if takes longer
timeout=150s

# this is how we invoke KM - with a timeout and reporting run time
function km_with_timeout () {
   local t=$timeout
   # Treat all before '--' as KM arguments, and all after '--' as payload arguments
   # With no '--', finding $ext (.km, .kmd. .so) has the same effect.
   # Note that we whitespace split KM args always, so no spaces inside of KM args are allowed
   while [ $# -gt 0 ]; do
      case "$1" in
         --)
            shift
            break
            ;;
         --timeout)
            shift
            t=$1
            ;;
         --putenv)
            # The putenv arg may contain $ext, so grab the arg here to avoid *$ext below
            __args="$__args $1"
            shift
            __args="$__args $1"
            ;;
         *$ext)
            break
            ;;
         *)
            __args="$__args $1"
            ;;
      esac
      shift
   done
   KM_ARGS="$__args $KM_ARGS"

   /usr/bin/time -f "elapsed %E user %U system %S mem %M KiB (km $*) " -a -o $TIME_INFO \
      timeout --signal=SIGABRT --foreground $t \
         ${KM_BIN} ${KM_ARGS} "$@"
   s=$?; if [ $s -eq 124 ] ; then echo -e "\nTimed out in $t" ; fi
   return $s
}

# A workaround for 'run wait' returning 255 for any failure.
# For commands NOT using 'run' but put in the background, use this function to wait and
# check for expected eror code (or 0, if expecting success)
# e.g.
#     wait_and_check 124 - wait for the last job put into background
wait_and_check()
{
   s=0; wait %% || s=$? ; assert_equal $s $1
}

if grep -iq 'vendor_id.*:.*intel' /proc/cpuinfo
then
   check_hwbreak=yes
fi
# this is how we invoke gdb - with timeout
function gdb_with_timeout () {
   timeout --foreground $timeout \
      gdb "$@"
   s=$?; if [ $s -eq 124 ] ; then echo "\nTimed out in $timeout" ; fi ; return $s
}


# this is needed for running in Docker - bats uses 'tput' so it needs the TERM
TERM=xterm

bus_width() {
   bw=$(${KM_BIN} -V exit_value_test.km |& awk '/physical memory width/ {print $9;}')
   if [ -z "$bw" ] ; then bw=39 ; fi # default is enough memory, if the above failed
   echo $bw
}

check_optional_mem_size_failure() {
   if [ "${USE_VIRT}" = 'kvm' ]; then
      assert_success
   else
      assert_failure
      assert_line --partial "Only 512GiB physical memory supported with KKM driver"
   fi
}

# Setup and teardown for each test.
# Note that printing to stdout/stderr in these functions only shows up on errors.
# For print on success too, redirect to >&3

function setup() {
  skip_if_needed "$BATS_TEST_DESCRIPTION"
  echo --- Test script output:
}

teardown() {
   cat <<EOF
--- Command line:
${command}
--- Command output:
${output}
---
EOF
}


# Helper for generic tests skipping based on test description.
# Relies on lists:
#  `not_needed_{generic,static,dynamic,shared}` and
#  `todo_{generic,static,dynamic,shared}`
# These need to be defined in the actual test and contain lists of tests to skip.
# See km*.bats for example.

# Checks if word "$1" is in list "$2".
# Elements of $2 could be wildcards, but list has to be in ''
name_in_list() {
   set -o noglob
   for pattern in ${2} ; do
      if [[ _xx$1 == _xx$pattern ]] ; then set +o noglob; return 0 ; fi
   done
   set +o noglob
   return 1
}

# Skip test if it's on one of "skip" or "todo" lists
#
# $1 is BATS_DESCRIPTION string
#   We assume test name is BASH_DESCRIPTION part before '('
# $test_type is global
skip_if_needed() {
   test="$(echo $1 | sed -e 's/(.*//')"
   if [ -z "$test" ] ; then return; fi
   nn="not_needed_$test_type"
   todo="todo_$test_type"
   # note that lists have to have header and trailer space, so adding here
   if name_in_list $test " $not_needed_generic ${!nn} " ; then
      skip "(not needed in '$test_type' test pass)"
      return
   fi
   if name_in_list $test " $todo_generic ${!todo} " ; then
      skip "TODO add $test to '$test_type' test pass"
      return
   fi
}

# ensure all LOAD regions in a core file have the same file page offset and virtual address page offset.
check_kmcore() {
   readelf -l $1 | awk '/LOAD/ {if (substr($2, length($2)-2) != substr($3, length($3)-2)) exit 1}'
}
