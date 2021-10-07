#!/usr/bin/python3
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
# prepare linkline using json output from prepare_extension.py

import os
import json
import re
import logging
from functools import reduce

cpython_pattern = re.compile("^/.*/cpython")


def l_flags(mk_info):
    """
    Consolidate -l and -L flags
    """
    if len(mk_info) != 0:
        # get ldflags from mkinfo as list of lists, and glue them together in one
        all_l_flags = reduce(lambda x, y: x + y, [i["ldflags"] for i in mk_info])
        all_l_pathes = reduce(lambda x, y: x + y, [i["ldpaths"] for i in mk_info])
    else:
        all_l_flags = ""
        all_l_pathes = ""

    # remove duplicates and also remove cross-references to libs in the same package
    # Get the list of libs in this package by converting /path/libNAME.so to NAME
    already_in = ["-l" + os.path.splitext(os.path.basename(f["so"]))[0][3:] for f in mk_info]
    final = []
    for i in all_l_flags:
        if i not in final and i not in already_in:
            final.insert(0, i)
    finaL = []
    for i in all_l_pathes:
        if i not in finaL:
            finaL.insert(0, i)
    logging.info(f"Final -llist: {final}")
    return final, finaL


def base(name):
    """
    usr/local/lib/python3.9/lib-dynload/_bisect.cpython-39-x86_64-linux-gnu.so -> _bisect
    """
    logging.info(f"name: {name} -> {os.path.splitext(os.path.splitext(os.path.basename(name))[0])[0]}")
    return os.path.splitext(os.path.splitext(os.path.basename(name))[0])[0]


def process_file(file_name, take_list):
    """
    read mk_info from json created by prepare_extension.py and write linkline,
    taking only items from take_list if provided
    """
    location = os.path.realpath(os.path.dirname(file_name))
    with open(file_name, 'r') as f:
        mk_info = [l for l in json.load(f) if len(take_list) == 0 or base(l["so"]) in take_list]

    final, finaL = l_flags(mk_info)

    prefix = cpython_pattern.sub('.', location) + '/'
    with open(os.path.join(location, "linkline_km.txt"), 'w') as f:
        for line in mk_info:
            f.write(prefix + line["so"].replace(".so", ".km.symbols.o") + "\n")
        for line in mk_info:
            f.write(prefix + line["so"].replace(".so", ".km.lib.a") + "\n")
        if len(final) != 0:
            f.write(" ".join(final))
        if len(finaL) != 0:
            f.write(" " + " ".join([f"-L{os.path.join(location, i)}" for i in finaL]))
        f.write("\n")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Process json output from prepare_extension.py and generate linklike_km.txt")
    parser.add_argument('json_file', type=argparse.FileType('r'), help='JSON from prepare_extension.py')
    parser.add_argument('--take', type=argparse.FileType('r'), action='store',
                        help='A file with new line separated names of modules to take in the package being analyzed')
    parser.add_argument('--log', action="store", choices=['verbose', 'quiet'],
                        help='Logging level. Verbose prints all info. Quiet only prints errors. Default is errors and warnings')
    args = parser.parse_args()
    file_name = args.json_file.name
    if not args.log:
        logging.basicConfig(level=logging.WARNING)  # default
    else:
        if args.log == 'verbose':
            logging.basicConfig(level=logging.INFO)
        else:  # 'quiet'
            logging.basicConfig(level=logging.CRITICAL)
    take_list = ""
    if (args.take):
        take_list = [base(l) for l in args.take.read().split('\n') if not l.startswith('#') and len(l) > 1]
        logging.info(f"Taking list: {take_list} length {len(take_list)}")
    process_file(file_name, take_list)
