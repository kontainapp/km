#!/usr/bin/env python3
""" test_release_remote

    Test the release install process on a remote azure VM.
"""

import subprocess
import json
import logging

RESOURCE_GROUP = "kontain-release-testing"
RESOURCE_GROUP_LOCATION = "westus"
TESTING_VM_NAME = "kontain-release-testing-vm"
TESTING_VM_IMAGE = "Canonical:UbuntuServer:18.04-LTS:latest"
TESTING_VM_SIZE = "Standard_D2s_v3"
TESTING_VM_ADMIN = "kontain"


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
    """ clean up deletes everything
    """

    subprocess.run([
        "az", "group", "delete",
        "-y",
        "--name", RESOURCE_GROUP,
    ], check=False)


def ssh_execute(remote_ip, cmd):
    """ ssh_execute execute the cmd through ssh
    """
    ssh_execute_cmd = [
        "ssh",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        f"{TESTING_VM_ADMIN}@{remote_ip}",
        cmd,
    ]

    subprocess.run(ssh_execute_cmd, check=True)


def test(remote_ip):
    """ test

        Copy the local tests to the remote VM and execute.
    """

    logger = logging.getLogger("test")
    logger.info("start testing in %s", remote_ip)
    ssh_execute(remote_ip, "python3 --version")
    subprocess.run([
        "scp",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-r",
        "test_release_local",
        f"{TESTING_VM_ADMIN}@{remote_ip}:~/"
    ], check=True)
    ssh_execute(
        remote_ip, "sudo mkdir -p /opt/kontain ; sudo chown kontain /opt/kontain")
    ssh_execute(remote_ip, "sudo apt install -y gcc")
    ssh_execute(remote_ip, "sudo chmod 666 /dev/kvm")
    ssh_execute(
        remote_ip, "cd test_release_local; python3 test_release_local.py")
    logger.info("successfully tested")


def main():
    """ main method
    """

    logging.basicConfig(level=logging.INFO)
    try:
        remote_ip = setup()
        test(remote_ip)
    finally:
        clean_up()


main()
