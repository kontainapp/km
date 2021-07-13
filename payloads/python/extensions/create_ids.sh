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
# A simple MANUAL helper to copy .km.id files from build place to lib place.
# by default assumes that it runs from payload/python and copies .id files from Modules to Lib
#
# Supposed to make it easier to use static dlopen() by automating the copy of .km.id to the right location
[ "$TRACE" ] && set -x

modules="${1}"
if [[ -z "$modules" ]] ; then echo "Usage: ${BASH_SOURCE[0]} modules [sources_loc] [libs_loc] [version]" ; exit 1; fi

sources_loc=${2:-"cpython/Modules"}
libs_loc=${3:-"cpython/Lib"}
python_layout="yes"
# pass VERS as env if 3.8 is not what you need
VERS=${VERS:-3.8}

for m in $modules ; do
   if [[ ! -z "$python_layout" ]] ; then
      from="$sources_loc/$m/build/lib.linux-x86_64-${VERS}"
   else
      from="$sources_loc"
   fi
   if [ ! -d $from ] ; then echo Please check if you have passed correct Python VERS as env var ; exit 1; fi
   if [ -d $from/$m ] ; then from="$from/$m" ; fi #  there is a 'module-name' dir for multimodule packages

   to=$libs_loc
   if [ -d $to/$m ] ; then to=$to/$m ; fi #  there is a 'module-name' dir for multimodule packages
   id_files=$(cd $from; find . -name '*km.id')
   if [[ ! -z "$id_files" ]] ; then
      echo Copy .km.id files from $from '->' $to
      tar -cf - -C $from  $id_files | tar -C $to -xf -
   else
      echo "No .km.id files found in $from"
   fi
done
