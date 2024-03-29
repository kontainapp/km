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
#   ./analyze_modules.sh "module_names" [config_file]
# Analyze module_name info (using google command line and github API) and
# generate module definition, optionally inserting it in the config_file
#
# Note that VERSION info may be missing (usually due to rate limit in github API),
# so you may need to manually update it before next steps work.
# VERSION is the tag name from github Release page for the module
#
#  After the info is in extension/modules.json, here are the steps to create a custom python.km
#     make build-modules pack-modules ALL_MODULES=module_name
#     manually add '{ "name" : "$module" }' to a KM config (e.g. extension/python-custom.json)
#     make custom

[ "$TRACE" ] && set -x
cd "$( dirname "${BASH_SOURCE[0]}")/.."   # We want to run in payloads/python.
python_libs=./cpython/Lib

usage() {
   cat <<EOF
Usage:  ${BASH_SOURCE[0]} module_name [config_file]
  Output json for a python module module_name. Gets info optmistically from google.
  If config_file is present, adds the info to .modules[] array there.
  Warning: as a side effect, it will pip3 install module into your ~/.local/lib/python...
EOF
   exit 1
}

# analyze 'module_name'
analyze()  {
   module=$1

   module_path=$module  # some modules have different paths (e.g. Pillow->PIL), so prepare for that
   module_info=$(googler -n 1 -C --json "github $module" )
   kontain_repo=kpython/${module,,}  # repo name is lowercase
   git_url=$(echo $module_info | jq -r .[].url)
   source_repo=${git_url#https://github.com/}  # git repo name ("owner/repo")
   release_info=$(curl -H 'User-Agent: kontainapp' --silent https://api.github.com/repos/$source_repo/releases/latest)
   latest_tag=$(echo $release_info  | jq -r  .tag_name)
   # Note: For some repos github API returns no release info ,e.g. for pytz.
   # I did not investigate why. We can try to fetch it via /tags then, but there is no way to find
   # out which tag relates to the latest release. so we just fail.
   if [ "$latest_tag" == "null" ] ; then
            latest_tag="NOT_FOUND api_reply: $(echo $release_info | jq -r .message)"
   fi

   if [ ! -d $python_libs/$module_path ] ; then pip3 install -q -t $python_libs $module; fi
   # make sure pip3 can find module in $python_libs
   export PYTHONPATH=$python_libs$(python3 -c 'import sys; print(":".join(sys.path))')
   if pip3 show -f $module 2>/dev/null | grep -q '\.so$'  ; then
      hasSo=true;
      repo=",
   \"dockerRepo\": \"$kontain_repo\""
   else
      hasSo=false;
   fi

   # prepare dependencies string
   deps=$(pip3 show -v $module | sed -n -e 's/,/ /g' -e 's/Requires: \(.*\)/\1/p')
   if [[ -n "$deps" ]] ; then
      deps=",
   \"dependsOn\" : { \"modules\": \"$deps\" }"
   fi

   # Now form the final json blob
   result=$(cat <<EOF
{
   "name" : "$module",
   "git" : "$git_url",
   "abstract" : "$(echo $module_info | jq -r .[].abstract | sed 's/"/\\"/g')",
   "versions": ["$latest_tag"],
   "hasSo": "$hasSo",
   "status": "unknown"${repo}${deps}
}
EOF
   )

   # either insert into config file, or simply print out
   if [ -n "$config_file" ] ; then
      tmpfile=/tmp/$(basename $config_file)_$$
      jq --indent 3 -r ".modules[.modules | length] += $result " $config_file > $tmpfile || exit 1
      mv $tmpfile $config_file
   else
      echo "$result"
   fi
}

modules=${1}
if [[ -z "$modules" || $module == "--help" ]] ; then
   usage
fi
config_file=${2}
if [[ -n "$config_file" && ! -f $config_file ]] ; then
   echo -e "$config_file not found\n"
   usage
fi

# now analyze !
for module in $modules ; do analyze $1 ; done
