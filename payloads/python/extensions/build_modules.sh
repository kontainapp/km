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
# Installs and builds missing Python modules, based on extension/modules.json content
#
# See extensions/README.md for more details on extensions
#
set -e ; [ "$TRACE" ] && set -x

# we assume the script is in python/extensions, this puts us in payloads/python
cd "$( dirname "${BASH_SOURCE[0]}")/.."
if [ ! -d cpython/Modules ]; then
    echo "cpython/Modules is missing, can't do much "
    exit 1
fi

pip3 install --user -q setuptools

cleanup() {
   echo Exiting ...
}
trap cleanup SIGINT SIGTERM

usage() {
   cat <<EOF
Usage:  ${BASH_SOURCE[0]} [options] [module_list]
  Options:
  --[build|generate|pack|pull|push] Operation requested. Default 'build'.
                                    'build' is 'clone+compile+generate+pack'
                                    'generate' creates Kontain specific files
                                    Pack packages in (pushable) local docker image
    module_list   Build named modules (space separated list), no matter if validated or not.
                  By default, we use validated modules from modules.json. I
EOF
   exit 1
}

# set defaults for global vars.
ext_file=$(realpath extensions/modules.json)
generate_files=$(realpath extensions/prepare_extension.py)

get_validated_module_names() {
   cat $ext_file | jq -r ".modules[] | select (.hasSo==\"true\") | select(.status | IN(\"validated\", \"built\")) | .name"
}

# first arg is module name , second the field to fetch
get_module_data() { #
   value=$(cat $ext_file | jq -r ".modules[] | select (.name==\"$1\") | .$2")
   if [ "$value" == "null" ] ; then value=""; fi
   echo "$value"
}
get_module_repo() { get_module_data "$1" dockerRepo; }

# build_one_module name version git_url
# Loads/builds modules, generates files and builds.a. Returns module name (if .a is present) or ""
# Expects to be executed in cpython dir
build_one_module() {
   name=$1     # module name
   if [[ `get_module_data $name hasSo` != true ]] ; then
      echo "*** Warning: No .so files in $name. Just 'pip3 install' into cpython/Lib or your virtual env"
      return
   fi
   version=$(get_module_data $name 'versions[-1]')
   url=$(get_module_data $name git)
   deps="$(get_module_data $name dependsOn)"
   setupLocation="$(get_module_data $name setupLocation)"

   echo build_one_module: name=$name version=$version url=$url mode=$mode
   if [[ -z "$url" || -z "$version" ]] ; then
      echo "*** Warning: no URL or VERSION for $name in $ext_file. Skipping '$mode' for $name"
      return
   fi
   if [[ $mode != build && $mode != generate ]] ; then echo ERROR: wrong mode; exit 1; fi
   src=Modules/$name
   if [[ $mode != generate ]] ; then # clone and build module
      if [[ -z "$url" ]] ; then echo "*** ERROR - no URL found. Please add git remote URL for $name" ; return; fi
      if [[ -n "$deps" && "$deps" != null ]] ; then echo "*** WARNING - $m needs '$deps', please make sure it is installed"; fi
      rm -rf $src
      # clone build the module with keeping trace and compile info for further processing
      # note: *do not* use '-j' (parallel jobs) flag in setup.py. It breaks some module builds. e.g. numpy
      echo === git clone $url -b $version $src
      git clone $url -b $version $src
   fi

   pushd $src/$setupLocation ; python3 setup.py build |& tee bear.out ; popd
   make_cmd=$(${generate_files} $src/$setupLocation/bear.out | grep 'make -C')
   echo $make_cmd
   $make_cmd
   # TODO: use create_ids.sh to automatically copy IDs
}

# generate/build neccessary .a/.o files for static dlopen()
build() {
   cd cpython
   for m in $modules ; do
      build_one_module $m
   done
   cd ..; pack # if all built fine, re-pack them
}

generate() { build; } # same code with small if/then/else inside. Used for help in debuggin. mainly

# Build Docker containers with module-specific km artifacts
pack() {
   loc=$(pwd)
   for name in $modules ; do
		cd $loc/cpython/Modules/$name
      echo === pack: $name $(pwd)
      # We need bash to explode the regexp and vars here in the loop. so need literals
      tar -cf $name.tar $(echo "dlstatic_km.mk.json linkline_km.txt `find build -regextype egrep -regex '.*(\.km.*[aod]|.*\.a)'` ")
      cat <<EOF | docker build -q -f - . -t $(get_module_repo $name)
FROM scratch
LABEL Description="$name python module binaries for customer python.km" Vendor="Kontain" Version="0.1"
ADD $name.tar /$name/
EOF
   done
}

pull() {
	for name in $modules ; do
		# echo SKIP docker pull kpython/py-$$name
		container=$(docker create $(get_module_repo $name) true)
		if [ -z "$container" ] ; then
			echo "*** Error: failed to load container with Kontain binaries for $name"
         continue
		fi
		echo Unpacking $name
      docker export $container | tar -C cpython/Modules -xf - $name
		silent_remove=$(docker rm $container)
	done
}

push() {
   make login
	for name in $modules ; do
		docker push $(get_module_repo $name)
	done
}

# Now check the args and execute the operation

while [ $# -gt 0 ]; do
  case "$1" in
    --help | help )
      usage
      ;;
    --build | --generate | --pack | --push | --pull)
      mode=${1#--}
      ;;
    *) # positional args started
      if [ -n "$modules" ] ; then usage ; fi # only 1 positional arg is ok
      modules="${1}"
      ;;
  esac
  shift
done

mode=${mode:-build}
modules=${modules:-$(get_validated_module_names)}

eval $mode
