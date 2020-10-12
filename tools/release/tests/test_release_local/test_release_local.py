#!/usr/bin/env python3
# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.


""" test_release_local

    Test the release install process on the local machine. Make sure
    /opt/kontain is created with proper permission, since the install
    script requires it.
"""

import os
import tempfile
import shutil
import subprocess
import argparse

OPT_KONTAIN = "/opt/kontain"
OPT_KONTAIN_BIN = f"{OPT_KONTAIN}/bin"
KONTAIN_GCC = f"{OPT_KONTAIN_BIN}/kontain-gcc"
KM = f"{OPT_KONTAIN_BIN}/km"
INSTALL_URL = "https://raw.githubusercontent.com/kontainapp/km-releases/master/kontain-install.sh"


def main():
    """ main method """

    parser = argparse.ArgumentParser()
    parser.add_argument("--version", help="version of km to be tested")
    args = parser.parse_args()

    # Clean up the /opt/kontain so we have a clean test run
    subprocess.run(["rm", "-rf", f"{OPT_KONTAIN}/*"], check=False)

    # Download and install
    install_cmd = f"wget {INSTALL_URL} -O - -q | bash"
    if args.version is not "":
        install_cmd = f"wget {INSTALL_URL} -O - -q | bash -s {args.version}"

    os.system(install_cmd)

    # Test: compile helloworld with kontain-gcc
    work_dir = tempfile.mkdtemp()
    shutil.copytree("assets", os.path.join(work_dir, "assets"))
    subprocess.run([
        KONTAIN_GCC,
        os.path.join(work_dir, "assets", "helloworld.c"),
        "-o",
        "helloworld",
    ], cwd=work_dir, check=True)
    subprocess.run([
        KM,
        "helloworld",
    ], cwd=work_dir, check=True)

    # Clean up
    shutil.rmtree(work_dir)


main()
