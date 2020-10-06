#!/bin/bash
#
# This is a wrapper to run a test suite. We want it separate from Makefile
# so we don't need to place all out Make infra (e.g. ../make/locations.mk) into
# test containers
#
#
set -e
[ "$TRACE" ] && set -x

cd $(dirname ${BASH_SOURCE[0]})

DEFAULT_TESTS="km_core_tests.bats"
DEFAULT_TEST_TYPE="static dynamic so native_static"

usage() {
cat <<EOF
Usage:  ${BASH_SOURCE[0]} [options]
  Options:
  --tests=bats_file_name  Run tests from  bats_file_name (default "$DEFAULT_TESTS")
  --test-type="types"     Space separated test types (default "$DEFAULT_TEST_TYPE")
  --pretty                Pretty colorfule output instead of TAP (https://testanything.org/) output
  --match=regexp          Only run tests with names matching regexp (default .*)
  --time-info-file=name   Temp file with tests time tracing (default /tmp/km_test_time_info_$$)
  --km=km_name            KM path. (default derived from git )
  --km-args="args...."    Optional argument to pass to each KM invocation
  --ignore-failure        Return success even if some tests fail.
  --dry-run               print commands instead of executing them
  --usevirt=virtmanager   optional argument to override virtualization platform kvm or kkm
  --jobs=count            optional argument to override parallel runs. Default is the result of nproc command (`nproc`)
EOF
}

bats_src_generated=km-bats-$$  # generated files need to be in ./ to avoid messing with 'load' command
SIGINT_exit_code=130 # by linux agreement, 128 + SIGINT. Use it for all trap exits

# arg1 is optional exit code. If not passed, we assume ^C
cleanup_and_exit() {
   echo Cleaning up...
   exit_code=${1:-$SIGINT_exit_code}
   $DEBUG rm -f ${bats_src_generated}.*
   $DEBUG rm -f ${time_info_file}
   exit $exit_code
}
trap cleanup_and_exit SIGINT SIGTERM

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
    --test-type=*)
      test_type="${1#*=}"
      ;;
    --usevirt=*)
      usevirt="${1#*=}"
      ;;
    --jobs=*)
      jobs_count="${1#*=}"
      ;;
    *)
      usage
      exit 1
  esac
  shift
done

pretty=${pretty:--t}
if [ "$pretty" == "-p" ] ; then
   RED="\033[31m"
   GREEN="\033[32m"
   NOCOLOR="\033[0m"
fi

tests=${tests:-"$DEFAULT_TESTS"}
match=${match:-'.*'}
time_info_file=${time_info_file:-/tmp/km_test_time_info_$$}
ignore_failure=${ignore_failure:-no}
test_type=${test_type:-"$DEFAULT_TEST_TYPE"}
jobs_count=${jobs_count:-$(nproc)}
if [ $jobs_count != 1 ] ; then
   # adding --jobs param delays log prining till the end of the run, so let's add it only
   # if it's really parallel
   echo -e "${GREEN}**Running tests with --jobs $jobs_count${NOCOLOR}"
   jobs="--jobs $jobs_count"
fi

# find km_bin if --km was not passed. Try to use ./km first, then revert to git
km_bin="${km}"
if [ -z "$km_bin" ] ; then
   if [ -x km ] ; then
      km_bin=$(realpath km)
   else
      km_bin="$(git rev-parse --show-toplevel)/build/km/km"
   fi
fi

if [ "$usevirt" != 'kvm' ] && [ "$usevirt" != 'kkm' ] ; then
   if [ -c '/dev/kvm' ] ; then
      usevirt="kvm"
   elif [ -c '/dev/kkm' ] ; then
      usevirt="kkm"
   else
      echo -e "${RED}**ERROR** '$usevirt' not in kvm or kkm. Only kvm or kkm based virtualization supported.${NOCOLOR}"
      exit 1
   fi
fi

device_node="/dev/$usevirt"
if [ ! -c $device_node ] ; then
   echo -e "${RED}**ERROR** char device check for '$device_node' failed.${NOCOLOR}"
   exit 1
fi

echo -e "${GREEN}**Running tests with virtualization $usevirt.${NOCOLOR}"

if [ ! -x $km_bin ] ; then
   echo -e "${RED}**ERROR** '$km_bin' does not exist or does not have exec permission ${NOCOLOR}"
   exit 1
fi

$DEBUG export TIME_INFO=$time_info_file
$DEBUG export KM_BIN=$km_bin
$DEBUG export USE_VIRT=$usevirt

# Generate files for bats, one file per type (e.g. km), from source
for t in $test_type ; do
   tmp_file=${bats_src_generated}.$t
   echo export KM_TEST_TYPE=$t > $tmp_file
   # we want the line  numbers in the tmp file to match the original
   # remove 2nd line - it is an empty comment in Kontain file headers
   sed 2d $tests >> $tmp_file
   test_list="$test_list $tmp_file"
done

$DEBUG bats/bin/bats $jobs $pretty -f "$match" $test_list
exit_code=$?
if [ $exit_code == 0 ] ; then
   echo '------------------------------------------------------------------------------'
   echo -e "${GREEN}Tests slower than 0.1 sec:${NOCOLOR}"
   if [[ -f "$time_info_file" ]] ; then
      $DEBUG grep elapsed $time_info_file | grep -v "elapsed 0:00.[01]" | sort -r
   else
      echo No time data is available.
   fi
   echo '------------------------------------------------------------------------------'
   echo ""
fi

if [ $ignore_failure == "yes" ] ; then
   exit_code=0
fi
cleanup_and_exit $exit_code
