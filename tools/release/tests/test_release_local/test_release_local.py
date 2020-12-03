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

DOCKER_CONFIG_DIR = "/etc/docker"
DOCKER_CONFIG_FILE = f"{DOCKER_CONFIG_DIR}/daemon.json"

def run_kontainer():
    """
    Configure krun into docker
    Start docker
    Pull the kontain python image
    Run a simple container using krun as the runtime
    """
    # If we are missing libraries or the libs are the wrong version, surface that now.
    # With docker involved it is harder to know what failed.
    subprocess.run([
        f"{OPT_KONTAIN_BIN}/krun",
        "--help",
    ], check=True)
    subprocess.run([
        "sudo",
        "mkdir",
        "-p",
        DOCKER_CONFIG_DIR,
    ], check=True)
    subprocess.run([
        "sudo",
        "cp",
        "assets/daemon.json",
        DOCKER_CONFIG_FILE,
    ], check=True)
    subprocess.run([
        "sudo",
        "systemctl",
        "enable",
        "docker.service"
    ], check=True)
    subprocess.run([
        "sudo",
        "systemctl",
        "reload-or-restart",
        "docker.service"
    ], check=True)
    subprocess.run([
        "sudo",
        "docker",
        "pull",
        "kontainapp/runenv-python",
    ], check=True)
    # This runs python in the kontainer with the simple program following "-c"
    subprocess.run([
        "sudo",
        "docker",
        "run",
        "--runtime",
        "krun",
        "kontainapp/runenv-python",
        "-c",
        "import os; print(os.uname())",
    ], check=True)

def main():
    """ main method """

    parser = argparse.ArgumentParser()
    parser.add_argument("--version", help="version of km to be tested")
    args = parser.parse_args()

    # Clean up the /opt/kontain so we have a clean test run
    subprocess.run(["rm", "-rf", f"{OPT_KONTAIN}/*"], check=False)

    # Download and install
    install_cmd = f"wget {INSTALL_URL} -O - -q | bash"
    if args.version is not None and args.version != "":
        install_cmd = f"wget {INSTALL_URL} -O - -q | bash -s {args.version}"

    os.system(install_cmd)

    # See what we got in the tarball.
    subprocess.run([
        "ls",
        "-l",
        OPT_KONTAIN_BIN,
    ], check=True);

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

    # Run a container with krun
    run_kontainer()

    # Clean up
    shutil.rmtree(work_dir)


main()
