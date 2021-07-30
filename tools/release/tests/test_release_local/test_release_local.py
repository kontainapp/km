#!/usr/bin/env python3
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

INSTALL_URL = "https://raw.githubusercontent.com/kontainapp/km/master/km-releases/kontain-install.sh"

DOCKER_CONFIG_DIR = "/etc/docker"
DOCKER_CONFIG_FILE = f"{DOCKER_CONFIG_DIR}/daemon.json"

def run_kontainer():
    """
    Add krun to runtimes docker recognizes, start docker and run a container in krun runtime
    """
    # If we are missing libraries or the libs are the wrong version, let's discover that here.
    # With docker involved it is harder to know what failed.
    subprocess.run([ f"{OPT_KONTAIN_BIN}/krun", "--help" ], check=True)

    subprocess.run([ "sudo", "mkdir", "-p", DOCKER_CONFIG_DIR ], check=True)
    subprocess.run([ "sudo", "cp", "assets/daemon.json", DOCKER_CONFIG_FILE ], check=True)
    subprocess.run([ "sudo", "systemctl", "enable", "docker.service" ], check=True)
    subprocess.run([ "sudo", "systemctl", "reload-or-restart", "docker.service" ], check=True)
    subprocess.run([ "docker", "pull", "kontainapp/runenv-python" ], check=True)

    # This runs python in the kontainer with the simple program following "-c"
    # It should return something like this in stdout:
    # "posix.uname_result(sysname='Linux', nodename='420613c03875', release='5.12.6-300.fc34.x86_64.kontain.KVM', version='#1 SMP Sat May 22 20:42:55 UTC 2021', machine='x86_64')"
    result = subprocess.run([ "docker", "run", "--runtime", "krun", "kontainapp/runenv-python", "-c", "import os; print(os.uname())" ],
        capture_output=True, text=True, check=True)
    print(result.stdout);
    if "kontain'," not in result.stdout:
        raise ValueError("Kontainer returned unexpected output")

def main():
    """ main method """

    parser = argparse.ArgumentParser()
    parser.add_argument("--version", help="version of km to be tested")
    parser.add_argument("--token", help="access token to KM repo", required=True)
    args = parser.parse_args()

    # Clean up the /opt/kontain so we have a clean test run
    subprocess.run(["rm", "-rf", f"{OPT_KONTAIN}/*"], check=False)

    # Download and install
    # GITHUB_RELEASE_TOKEN is required to get access to private repos. The
    # token is the Github Personal Access Token (PAT)
   #  token = os.environ.get("GITHUB_RELEASE_TOKEN")
    if {args.token} is None:
        raise ValueError("--token is not set, cannot access private KM repo")
    install_cmd = f"wget --header \"Authorization: token {args.token}\" {INSTALL_URL} -O - -q | bash"
    if args.version is not None and args.version != "":
        install_cmd += f" -s {args.version}"

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
