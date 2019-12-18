#!/bin/bash
# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Installs and builds missing Python modules, based on extension/modules.txt content
#
# See extensions/README.md for more details on extensions
#
set -e
if [ -v BASH_TRACING ] ; then set -x ; fi
cd "$( dirname "${BASH_SOURCE[0]}")/.."  # we assume this script is located in python/extensions

if [ ! -d cpython/Modules ]; then
    echo "cpython/Modules is missing, can't do much "
    exit 1
fi

usage() {
cat <<EOF
Usage:  ${BASH_SOURCE[0]} [options]
  Options:
  --mode=[build|generate|all] Build modules with saving the log, or generate files for static link(default 'all')
EOF
}

cleanup() {
   echo Exiting ...
}
trap cleanup SIGINT SIGTERM

while [ $# -gt 0 ]; do
  case "$1" in
    --mode=*)
      mode="${1#*=}"
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

mode=${mode:-all}
if [[ ! ( $mode == 'build' || $mode == 'generate' || $mode == 'all' ) ]] ; then usage; exit 1 ; fi

ext_file=$(realpath extensions/modules.txt)
generate_files=$(realpath extensions/prepare_extension.py)
path_ids=$(realpath extensions/create_ids.sh)

pushd cpython

# Loads/builds modules, generates files and builds.a. Returns module name (if .a is present) or ""
# Expects to be executed in cpython dir
process_module() {
   name=$1     # module name
   version=$2  # requested version
   url=$3      # git url ... needed for modules with .so only. May be empty

   echo process_module: name=$name version=$version url=$url pwd=$PWD mode=$mode
   src=./Modules/$name

   if [[ ! -d Lib/$name ]] ; then
      pip3 install -t Lib $name
   fi
   # Note: when converting this file to Python, this is how to run pip3 from Python (for the next PR):
   # import subprocess
   # import sys
   # def install(package):
   #     subprocess.call([sys.executable, "-m", "pip", "install", package])

   if [[ -z "$(find ./Lib/$name -name '*.so')" ]] ; then
      echo $installed does not seem to have any .so files. Skipping...
      return
   fi
   if [[ ! -d $src ]] ; then
      echo === $src not found, cloning ...
      if [[ -z "$url" ]] ; then echo *** ERROR - no URL. Please add git remote URL for $name ; return; fi
      (cd Modules; git clone $url)
   fi

   cd $src
   if [[ $mode == build || $mode == all ]] ; then
      # build module if it was not built yet
      echo Checking $src and rebuilding it if needed...
      if [[ ! -f bear.out ]] ; then
         echo === setup.py build $module
         git clean -xdf
         git checkout $version
         # build the module with keeping trace and compile info for further processing
         # note: *do not* use '-j' (parallel jobs) flag in setup.py. It breaks some module builds. e.g. numpy
         python3 setup.py build |& tee bear.out
      fi
   fi

   if [[ $mode == generate || $mode == all ]] ; then
      make_cmd=$(${generate_files} bear.out | grep 'make -C')
      echo $make_cmd
      $make_cmd
      $path_ids $name $(realpath ..) $(realpath ../../Lib)
   fi
}

# scan modules.txt and generate/build neccessary .a/.o files for static dlopen()
loc=$(realpath $PWD)
if [[ -f $ext_file ]] ; then
   if ! pip3 show -q jinja2 ; then pip3 install --user jinja2; fi # F31 has it preinstalled, but for F30 we need it, will drop when all switch to F31
   cat $ext_file | while read mod_name mod_version mod_src_git dependencies; do
      if [[ -z "$mod_name" || $mod_name =~ ^# ]] ; then continue ; fi
      for d in $dependencies ; do
         if [[ -z "$(type -t $d)" ]] ; then
            echo "*** WARNING $mod_name needs '$d' which is not found, please install the neccessary package"
            missing=yes
         fi
      done
      if [[ "$missing" == "yes" ]] ; then
         echo "*** WARNING $mod_name skipped "
      else
         echo =========== processing $mod_name ===
         process_module  $mod_name $mod_version $mod_src_git
         cd $loc
      fi
   done
fi
