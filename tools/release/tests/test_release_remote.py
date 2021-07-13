#!/usr/bin/env python3
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

""" test_release_remote

    Test the release install process on a remote azure VM.
"""

import subprocess
import json
import logging
import argparse
import time
import os

RESOURCE_GROUP = f"kontain-release-testing-{time.monotonic_ns()}"
RESOURCE_GROUP_LOCATION = "westus"
TESTING_VM_NAME = "kontain-release-testing-vm"
#TESTING_VM_IMAGE = "Canonical:UbuntuServer:18.04-LTS:latest"
TESTING_VM_IMAGE = "Canonical:0001-com-ubuntu-server-focal:20_04-lts:latest"
TESTING_VM_SIZE = "Standard_D2s_v3"
TESTING_VM_ADMIN = "kontain"

TESTING_DEFAULT_VERSION = "v0.1-test"


def validate_version(version):
    """ validate_version

        Validate the formate the the version string. Version should start either:
        * v*
        * refs/tags/v* (from azure pipeline)
        * refs/heads/* (from azure pipeline testing branch) -> default version v0.1-test
    """

    logger = logging.getLogger("validate_version")

    if version is None or version == "":
        logger.warning(
            "No version is set. Will use default latest version from install")
        return version

    if version.startswith("refs/tags/v"):
        clean_version = version[len("refs/tags/"):]
    elif version.startswith("refs/heads/"):
        logger.warning(
            "Release is triggered via branch %s. Using default version %s", version, TESTING_DEFAULT_VERSION)
        clean_version = TESTING_DEFAULT_VERSION
    else:
        clean_version = version

    if not clean_version.startswith("v"):
        logger.warning(
            "Version %s is not conforming to v* pattern.", clean_version)

    return clean_version


def setup():
    """ setup

        * create a new resource group for testing. this is easier for cleanup.
        * create the vm needed for testing.
    """

    logger = logging.getLogger("setup")
    logger.info("creating a new resource group...")
    subprocess.run([
        "az", "group", "create",
        "--location", RESOURCE_GROUP_LOCATION,
        "--name", RESOURCE_GROUP,
    ], check=True)
    logger.info("successfully created a new resource group")

    logger.info("creating a new vm for testing...")
    ret = subprocess.run([
        "az", "vm", "create",
        "--resource-group", RESOURCE_GROUP,
        "--name", TESTING_VM_NAME,
        "--image", TESTING_VM_IMAGE,
        "--size", TESTING_VM_SIZE,
        "--admin-username", TESTING_VM_ADMIN,
    ], stdout=subprocess.PIPE, check=True)
    print(ret.stdout)
    logger.info("successfully created a new vm")

    output = json.loads(ret.stdout)

    return output["publicIpAddress"]


def clean_up():
    """ clean up deletes everything """

    logger = logging.getLogger("clean_up")
    logger.info("Starts to clean up")

    subprocess.run([
        "az", "group", "delete",
        "-y", "--no-wait", "--debug",
        "--name", RESOURCE_GROUP,
    ], check=False)

    logger.info("Clean up successful")


def ssh_execute(remote_ip, cmd, logger):
    """ ssh_execute execute the cmd through ssh """

    ssh_execute_cmd = [
        "ssh",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        f"{TESTING_VM_ADMIN}@{remote_ip}",
        cmd,
    ]
    logger.info("ssh execute: %s", ssh_execute_cmd)

    subprocess.run(ssh_execute_cmd, check=True)


def test(remote_ip, version, token):
    """ test

        Copy the local tests to the remote VM and execute.
    """

    logger = logging.getLogger("test")
    logger.info("start testing in %s", remote_ip)

    # Sometimes, the VM is not completely ready when IP address is returned. In
    # this case we need to retry for the first ssh command.
    max_retry = 3
    run = 0
    while run < max_retry:
        try:
            ssh_execute(remote_ip, "python3 --version", logger)
        except subprocess.CalledProcessError:
            if run + 1 == max_retry:
                raise

            logger.warning(
                "Failed ssh execute... Retry %d out of %d", run + 1, max_retry)
            time.sleep(30)
            continue
        else:
            break
        finally:
            run += 1

    subprocess.run([
        "scp",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-r",
        "test_release_local",
        f"{TESTING_VM_ADMIN}@{remote_ip}:~/"
    ], check=True)
    ssh_execute(
        remote_ip, "sudo mkdir -p /opt/kontain ; sudo chown kontain /opt/kontain", logger)
    ssh_execute(remote_ip, "/usr/bin/cloud-init status --wait", logger)
    ssh_execute(remote_ip, "sudo apt-get update", logger)
    ssh_execute(remote_ip, "sudo apt-get install -y gcc docker.io libyajl2 libseccomp2 libcap2", logger)
    ssh_execute(remote_ip, "sudo chmod 666 /dev/kvm", logger)
    ssh_execute(remote_ip, f"sudo usermod -G docker {TESTING_VM_ADMIN}", logger)

    if version is None or version == "":
        version_flag = ""
    else:
        version_flag = f"--version {version}"

    ssh_execute(
        remote_ip, f"cd test_release_local; python3 test_release_local.py {version_flag} --token=${token}", logger)
    logger.info("successfully tested")


def main():
    """ main method """

    parser = argparse.ArgumentParser()
    parser.add_argument("--version", help="version of km to be tested")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO)
    # GITHUB_RELEASE_TOKEN is required to get access to private repos. The
    # token is the Github Personal Access Token (PAT)
    token = os.environ.get("GITHUB_RELEASE_TOKEN")
    if token is None:
        raise ValueError("GITHUB_RELEASE_TOKEN is not set, cannot access private KM repo")
    try:
        version = validate_version(args.version)
        remote_ip = setup()
        test(remote_ip, version, token)
    finally:
        clean_up()

main()
