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
# Build script for python.
# Also tries to add modules enumberated in m_list (needs `bear' installed for this)
#
set -ex

if [ ! -d cpython ]; then
    set -x
    git clone https://github.com/python/cpython.git -b v3.7.4
    pushd cpython
    ./configure
    patch -p1 < ../unittest.patch
    patch -p0 < ../makesetup.patch
else
    pushd cpython
fi

# NOTE: we are in cpython dir from now on
cp ../Setup.local Modules/Setup.local

# TODO:  replace hardcoded arrays with external info
#        can use this externally:
# modules=`git status --untracked-files --short |  awk -F/ '/Modules/ {print $2}'`
# m_list=(falcon hug werkzeug markupsafe)
# m_version=(2.0.0 someversion 0.16.0 2.6.0)
m_list=()
m_version=()
extract_build_info=`pwd`/../extensions/extract_build.py

# scan modules ($i is index im m_list), transforms bear out and add to Setup.local
for i in ${!m_list[@]} ; do
   module=./Modules/${m_list[$i]}
   echo === $0: checking module $module from `pwd`
   if [[ ! -d $module ]] ; then echo === MISSING MODULE, SKIPPING; continue; fi
   pushd $module
   # skip module build if it was already built and analyzed
   if [[ ! -f compile_commands.json ]] ; then
      echo === setup.py build $module
      git clean -xdf ; python3 setup.py clean
      bear python3 setup.py build > bear.out
   fi
   setup_info=km_setup.local
   if [[ ! -f $setup_info ]] ; then
      $extract_build_info
      if [[ $? -ne 0 ]] ; then popd; continue; fi  # skip python-only modules
   fi
   echo "=== Adding $module/$setup_info to Modules/Setup.local. Lines added: $(cat $setup_info | wc -l)"
   cat $setup_info >> ../Setup.local
   popd # back to cpython
   echo === TODO: version should be ${m_version[$i]}. Also check for '-L -l' in km_libs.json
done

make -j`expr 2 \* $(nproc)`
popd # back to payloads/python

# TODO: sitecustomize should be installed in the site location.
# The one below is good if PYTHONPATH is set to Lib or it's a build run.
cp extensions/km_sitecustomize.py cpython/Lib/sitecustomize.py

./link-km.sh
echo ""
echo "now in cpython you can run ``../km/build/km/km ./python.km''"

