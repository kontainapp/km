#!/usr/bin/env python3

""" test_release_local

    Test the release install process on the local machine. Make sure
    /opt/kontain is created with proper permission, since the install
    script requires it.
"""

import os
import tempfile
import shutil
import subprocess

OPT_KONTAIN = "/opt/kontain"
OPT_KONTAIN_BIN = f"{OPT_KONTAIN}/bin"
KONTAIN_GCC = f"{OPT_KONTAIN_BIN}/kontain-gcc"
KM = f"{OPT_KONTAIN_BIN}/km"
INSTALL_URL = "https://raw.githubusercontent.com/kontainapp/km-releases/master/kontain-install.sh"


def main():
    """ main method """

    # Clean up the /opt/kontain so we have a clean test run
    subprocess.run(["rm", "-rf", f"{OPT_KONTAIN}/*"], check=False)

    # Download and install
    os.system(f"wget {INSTALL_URL} -O - -q | bash")

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
