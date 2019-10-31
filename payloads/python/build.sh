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
set -e
# set -x

if [ ! -d cpython ]; then
    echo Fetching cpython from github...
    git clone https://github.com/python/cpython.git -b v3.7.4
    pushd cpython
    echo Configuring...
    ./configure
    patch -p1 < ../unittest.patch
    patch -p0 < ../makesetup.patch
else
    pushd cpython
fi

# We are in cpython dir from now on. setup_file will be used further
setup_file=$(realpath Modules/Setup.local)
ext_file=$(realpath ../extensions/modules.txt)
extract_build=$(realpath ../extensions/extract_build.py)

cp ../Setup.local $setup_file

# Gets module if needed, and build/prep info for Setup.local
process_module() {
   name=$1     # module name
   version=$2  # requested version
   url=$3      # git url ... needed for modules with .so only. May be empty

   echo processing name=$name version=$version url=$url pwd=$PWD
   src=./Modules/$name
   installed=./Lib/$name

   if [[ ! -d $installed ]] ; then
      pip3 install -t Lib $name
   fi
   if [[ -z "$(find $installed -name '*.so')" ]] ; then
      echo $installed does not seem to have any .so files. Skipping...
      return
   fi
   if [[ ! -d $src ]] ; then
      echo === $src not found, cloning ...
      if [[ -z "$url" ]] ; then echo *** ERROR - no URL. Please add git remote URL for $name ; return; fi
      (cd Modules; git clone $url)
   fi

   cd $src
   # build module if it was not built yet
   echo Checking $src and rebuilding if needed...
   if [[ ! -f compile_commands.json ]] ; then
      echo === setup.py build $module
      git clean -xdf
      git checkout $version
      # build the module with keeping trace and compile info for further processing
      # note: *do not* use '-j' (parallel jobs) flag in setup.py. It breaks some module builds. e.g. numpy
      bear python3 setup.py build |& tee bear.out
   fi

   $extract_build
   echo "=== Adding $name info to Modules/Setup.local. Lines added: $(cat km_setup.local | wc -l)"
   cat km_setup.local >> $setup_file
}

loc=$(realpath $PWD)
if [[ -f $ext_file ]] ; then
   if [[ -z "$(type -t bear)" ]] ; then
      echo "*** ERROR Modules build needs 'bear' which is not found, please install the neccessary package"
      exit 1
   fi
   cat $ext_file | while read mod_name mod_version mod_src_git dependencies; do
      if [[ -z "$mod_name" || $mod_name =~ ^# ]] ; then continue ; fi
      for d in $dependencies ; do
         if [[ -z "$(type -t $d)" ]] ; then
            echo "*** WARNING $mod_name needs '$d' which is not found, please install the neccessary package"
            missing=yes
         fi
      done
      if [[ -z "$missing" ]] ; then
         process_module  $mod_name $mod_version $mod_src_git
         cd $loc
      else
         echo "*** WARNING $mod_name skipped "
      fi
   done
fi

echo Building python...
make -j`expr 2 \* $(nproc)`
popd # back to payloads/python

# TODO: sitecustomize should be installed in the site location.
# The one below is good if PYTHONPATH is set to Lib or it's a build run.
cp extensions/km_sitecustomize.py cpython/Lib/sitecustomize.py

echo Linking python.km...
LDLIBS=$(echo `cat cpython/Modules/*/km_libs.txt`) ./link-km.sh
echo ""
echo "now in cpython you can run ``../km/build/km/km ./python.km''"

