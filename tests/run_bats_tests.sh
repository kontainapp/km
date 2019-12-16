#!/usr/bin/bash
#
# This is a wrapper to run a test suite. We want it separate from Makefile
# so we don't need to place all out Make infra (e.g. ../make/locations.mk) into
# test containers
#
#

usage() {
cat <<EOF
Usage:  ${BASH_SOURCE[0]} [options]
  Options:
  --tests=bats_file_name  Run tests from  bats_file_name (default "km_core_tests.bats km_so_tests.bat")
  --match=regexp          Only run tests with names matching regexp (default .*)
  --time-info-file=name   Temp file with tests time tracing (default /tmp/km_test_time_info_$$)
  --km=km_name            KM path. (default derived from git )
  --km-args="args...."    Optional argument to pass to each KM invocation
  --ignore-failure        Return success even if some tests fail.
  --pretty                Pretty colorfule output instead of TAP (https://testanything.org/) output
  --dry-run               print commands instead of executing them
EOF
}

cleanup() {
   echo Cleaning up...
   if [ -f $time_info_file ] ; then $DEBUG rm $time_info_file ; fi
}
trap cleanup SIGINT SIGTERM

while [ $# -gt 0 ]; do
  case "$1" in
    --tests=*)
      tests="${1#*=}"
      ;;
    --km=*)
      km="${1#*=}"
      ;;
    --km-args=*)
      export KM_ARGS="${1#*=}"
      ;;
    --match=*)
      match="${1#*=}"
      ;;
    --ignore-failure)
      ignore_failure="yes"
      ;;
    --time-info-file=*)
      time_info_file="${1#*=}"
      ;;
    --pretty)
      pretty="-p"
      ;;
    --dry-run)
      DEBUG=echo
      ;;
    *)
      usage
      exit 1
  esac
  shift
done

tests=${tests:-"km_core_tests.bats km_so_tests.bats km_dyn_tests.bats"}
match=${match:-'.*'}
time_info_file=${time_info_file:-/tmp/km_test_time_info_$$}
pretty=${pretty:--t}
ignore_failure=${ignore_failure:-no}
km_bin=${km:-"$(git rev-parse --show-toplevel)/build/km/km"}

if [ "$pretty" == "-p" ] ; then
   RED="\033[31m"
   GREEN="\033[32m"
   NOCOLOR="\033[0m"
fi

if [ ! -x $km_bin ] ; then
   echo -e "${RED}**ERROR** '$km_bin' does not exist or does not have exec permission ${NOCOLOR}"
   exit 1
fi


$DEBUG export TIME_INFO=$time_info_file
$DEBUG export KM_BIN=$km_bin
$DEBUG time bats/bin/bats $pretty -f "$match" $tests
exit_code=$?

if [ $exit_code == 0 ] ; then
   echo '------------------------------------------------------------------------------'
   echo -e "${GREEN}Tests slower than 0.1 sec:${NOCOLOR}"
   $DEBUG grep elapsed $time_info_file | grep -v "elapsed 0:00.[01]" | sort -r
   echo '------------------------------------------------------------------------------'
   echo ""
fi

cleanup
if [ $ignore_failure == "yes" ] ; then
   exit 0
else
   exit $exit_code
fi
