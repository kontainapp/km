#!/bin/bash
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
#
# use this if started as a standalone script:
if [ -z "$BATS_TEST_FILENAME" ] ; then "exec" "`dirname $0`/bats/bin/bats" "$0" "$@" ; fi

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

# set umask to 0. Some of the tests use 0666 as file create permissions.
umask 0

# KM binary location.
if [ -z "$KM_BIN" ] ; then
   echo "Please make sure KM_BIN env is defined and points to KM executable." >&3
   exit 10
fi

# virtualization type
if [ -z "$USE_VIRT" ] ; then
   echo "Please make sure USE_VIRT env is defined as 'kvm' or 'kkm'." >&3
   exit 10
fi

if [ -z "$TIME_INFO" ] ; then
   echo "Please make sure TIME_INFO env is defined and points to a file. We will put detailed timing info there">&3
   exit 10
fi

if [ -z "$BRANCH" ] ; then
   BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

# Now find out what test type we were asked to run ('static' is default)
# and set appropriate extension and km args.
#
# We also use this to set port range for different test types to allow for parallel testing.
# E.g. gdb or http tests require a port, and it need to be unique so parallel tests
# do not step on each other. We start from some arbirarty port and set a range of 1000 ports per test type
# (500 for KVM and 500 for KKM), and then within km_core_tests.bats will assign offset in tests which need a port
test_type=${KM_TEST_TYPE:-static}

case $test_type in
   static)
      ext=.km
      port_range_start=14777
      ;;
   dynamic)
      ext=.kmd
      port_range_start=15777
      ;;
   alpine_static)
      ext=.alpine.km
      port_range_start=16777
      ;;
   alpine_dynamic)
      ext=.alpine.kmd
      port_range_start=17777
      ;;
   so)
      ext=.km.so
      port_range_start=18777
      ;;
   glibc_static)
      ext=.fedora
      port_range_start=19777
      ;;
   glibc_dynamic)
      ext=.fedora.dyn
      port_range_start=20777
      ;;
   *)
      echo "Unknown test type: $test_type, should be 'static', 'dynamic', 'alpine_static', 'glibc_static', 'glibc_dynamic', 'alpine_dynamic' or 'so'."
      export KM_BIN=fail
      return 1
      ;;
esac

# USE_VIRT is exported by run_bats_tests.sh
if [[ "${USE_VIRT}" != kvm && "${USE_VIRT}" != kkm ]] ; then
   echo "Unknown virtualization type : ${USE_VIRT}"
   exit 10
fi

KM_ARGS="--virt-device=/dev/${USE_VIRT} $KM_ARGS"
if [[ "${USE_VIRT}" == kkm ]] ; then
   # make sure kvm and kkm use different port ranges for tests
   port_range_start=$(( $port_range_start + 500 ))
fi

# We will kill any individual test if takes longer than that
if [ -z "${VALGRIND}" ]; then
timeout=250s
else
timeout=1000s
fi

#
# this is how we invoke KM - with a timeout and reporting run time
# Just a reminder... We want a km command line that has the following order for args
# km kmargs ... [ ldso related args ] [ -- ] payloadname payloadargs ...
#
function km_with_timeout () {
   local t=$timeout

   # running under valgrind we need to remove LD_PRELOAD, it interferes with dynamic loader
   # we don't need to do that if --putenv is used or for non-dynamic tests
   if [[ ${test_type} == "dynamic" || ${test_type} == "alpine_dynamic" || ${test_type} == "glibc_dynamic" ]]; then
      local c_env=${VALGRIND:+--copyenv=LD_PRELOAD}
   else
      local c_env=
   fi

   # Treat all before '--' as KM arguments, and all after '--' as payload arguments
   # With no '--', finding $ext (.km, .kmd. .so) has the same effect.
   # Note that we whitespace split KM args always, so no spaces inside of KM args are allowed
   # The intention is that this loop only process km args.  Payload name and args should not
   # be examined.  For some ambiguous cases, the call to km_with_timeout() will need to have
   # the '--' flag to prevent this loop from straying into payload args.
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
            c_env=
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

   KM_ARGS="$KM_ARGS ${c_env} $__args"

   CMD="${KM_BIN} ${KM_ARGS}"

   /usr/bin/time -f "elapsed %E user %U system %S mem %M KiB (km $*) " -a -o $TIME_INFO \
      timeout --signal=SIGABRT --foreground $t \
         ${VALGRIND} ${CMD} "$@"
   # Per timeout(1) it returns 124 on timeout, and 128+signal when killed by signal
   s=$?; if [[ $s == 124  ]] ; then
      echo -e "\nTime out in $t : ${CMD} $@"
   elif [[ $s -gt 128 ]] ; then
      echo -e "\nKilled by signal. Timeout returns $s: ${CMD} $@"
   fi
   return $s
}

# A workaround for 'run wait' returning 255 for any failure.
# For commands NOT using 'run' but put in the background, use this function to wait and
# check for expected eror code (or 0, if expecting success)
# e.g.
#     wait_and_check pid 124 - wait for the last job put into background
wait_and_check()
{
   s=0; wait $1 || s=$? ; assert_equal $s $2
}

if grep -iq 'vendor_id.*:.*intel' /proc/cpuinfo
then
   check_hwbreak=yes
fi
# this is how we invoke gdb - with timeout
function gdb_with_timeout () {
   timeout --foreground $timeout \
      gdb --batch "$@"
   s=$?; if [ $s -eq 124 ] ; then echo "\nTimed out in $timeout" ; fi ; return $s
}


# this is needed for running in Docker - bats uses 'tput' so it needs the TERM
TERM=xterm

bus_width() {
   bw=$(${KM_BIN} -V --km-log-to=stderr exit_value_test.km |& awk '/physical memory width/ {print $9;}')
   if [ -z "$bw" ] ; then bw=39 ; fi # default is enough memory, if the above failed
   echo $bw
}

check_optional_mem_size_failure() {
   if [[ "${USE_VIRT}" == kvm ]]; then
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
  echo start: $(date --rfc-3339=ns) $BATS_TEST_DESCRIPTION
  echo --- Test script output:
}

teardown() {
   cat <<EOF
  end: $(date --rfc-3339=ns) $BATS_TEST_DESCRIPTION

--- Command line:
${command[*]}
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

# Stream the contents of the file passed as arg1 into the bats log file.
# Also put the value of arg2 into the header message.  arg2 has no defined meaning.
file_contents_to_bats_log() {
      echo "# === Begin contents of $1, arg2=$2 =====" >&3
      sed -e "s/^/# /" <$1 >&3
      echo "# === End contents of $1 =====" >&3
}
