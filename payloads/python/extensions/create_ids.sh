#!/bin/bash
#
# Copyright © 2019-2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
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
