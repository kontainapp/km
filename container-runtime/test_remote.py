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

""" test_remote.py

    Used to test `crun` remotely on an azure VM.
"""

import logging
import subprocess
import json
import abc
import time
import argparse
import uuid

RESOURCE_GROUP = "kontain-crun-testing"
RESOURCE_GROUP_LOCATION = "westus"
TESTING_VM_NAME = "kontain-crun-testing-vm"
# from: `az vm image list --all --offer fedora`
TESTING_VM_IMAGE = "Canonical:UbuntuServer:18.04-LTS:latest"
TESTING_VM_SIZE = "Standard_D2s_v3"
TESTING_VM_ADMIN = "kontain"


def get_random_id():
    return str(uuid.uuid4())[:8]


class RemoteTestAzure(metaclass=abc.ABCMeta):
    """ RemoteTestAzure """

    def __init__(self,
                 resource_group,
                 resource_group_location,
                 vm_name,
                 vm_image,
                 vm_size,
                 vm_admin):
        self.resource_group = resource_group
        self.resource_group_location = resource_group_location
        self.vm_name = vm_name
        self.vm_image = vm_image
        self.vm_size = vm_size
        self.vm_admin = vm_admin

        self.logger = logging.getLogger("RemoteTestAzure")

    def setup(self):
        """ setup

            * create a new resource group for testing. this is easier for cleanup.
            * create the vm needed for testing.
        """

        self.logger.info("creating a new resource group...")
        subprocess.run([
            "az", "group", "create",
            "--location", self.resource_group_location,
            "--name", self.resource_group,
        ], check=True)
        self.logger.info("successfully created a new resource group")

        self.logger.info("creating a new vm for testing...")
        ret = subprocess.run([
            "az", "vm", "create",
            "--resource-group", self.resource_group,
            "--name", self.vm_name,
            "--image", self.vm_image,
            "--size", self.vm_size,
            "--admin-username", self.vm_admin,
        ], stdout=subprocess.PIPE, check=True)
        self.logger.info("successfully created a new vm")

        output = json.loads(ret.stdout)

        return output["publicIpAddress"]

    def clean_up(self):
        """ clean up deletes everything """

        self.logger.info("Starts to clean up")

        subprocess.run([
            "az", "group", "delete",
            "--yes",
            "--no-wait",
            "--name", self.resource_group,
        ], check=False)

        self.logger.info("Clean up successful")

    def ssh_execute(self, remote_ip, cmd):
        """ ssh_execute execute the cmd through ssh """

        self.logger.info("Running: %s", cmd)

        ssh_execute_cmd = [
            "ssh",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            f"{self.vm_admin}@{remote_ip}",
            cmd,
        ]
        subprocess.run(ssh_execute_cmd, check=True)

        self.logger.info("Command successful: %s", cmd)

    def scp_to_remote(self, remote_ip, local, remote):
        """ scp_to_remote """

        subprocess.run([
            "scp",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-r",
            local,
            f"{self.vm_admin}@{remote_ip}:{remote}"
        ], check=True)

    @abc.abstractmethod
    def test(self, remote_ip):
        """ test will run the actual test

            By design, subclass will have to implement this method.
        """

    def wait_ready(self, remote_ip):
        """ wait_ready

            When self.setup returns, the VM is booted and assigned a public IP address,
            however, the VM may not be ready to accept ssh commands. This method makes
            sure ssh is ready by calling a simple `/bin/true` command. If ssh fails,
            it will retry.
        """
        max_retry = 3
        run = 0
        while run < max_retry:
            try:
                self.ssh_execute(remote_ip, "/bin/true")
            except subprocess.CalledProcessError:
                if run + 1 == max_retry:
                    raise

                self.logger.warning(
                    "Failed ssh execute... Retry %d out of %d", run + 1, max_retry)
                time.sleep(30)
                continue
            else:
                break
            finally:
                run += 1

    def run(self, cleanup_on_error=True):
        """ run """

        try:
            remote_ip = self.setup()
            self.wait_ready(remote_ip)
            self.test(remote_ip)
        except Exception:
            if cleanup_on_error:
                self.clean_up()
            else:
                self.logger.warning("Skipping clean up on error...")

            raise
        else:
            self.clean_up()


class CRUNRemoteTest(RemoteTestAzure):
    """ CRUNRemoteTest """

    def test(self, remote_ip):
        count=3 # apt-get is flaky lately , let's retry it a few times
        # Going forward we should put together an AMI with all that and skip this step
        tools_to_install = "make git gcc build-essential pkgconf libtool libsystemd-dev libcap-dev libseccomp-dev libyajl-dev libtool autoconf python3 automake"
        while count >= 0:
          try:
            self.ssh_execute(remote_ip, "sudo apt-get update")
            self.ssh_execute(remote_ip, f"sudo apt-get install -y {tools_to_install}")
            break
          except:
            print("apt-get failed , let's see if retry helps")
            count -= 1
            if count == 0:
               raise

        self.scp_to_remote(remote_ip, "crun", "~/")
        self.scp_to_remote(remote_ip, "/opt/kontain/bin/km", "~/km")
        self.ssh_execute(
            remote_ip, "sudo mkdir -p /opt/kontain/bin; sudo mv ~/km /opt/kontain/bin/km")
        self.scp_to_remote(remote_ip, "/opt/kontain/runtime/libc.so",
                           "~/libc.so")
        self.ssh_execute(
            remote_ip, "sudo mkdir -p /opt/kontain/runtime; sudo mv ~/libc.so /opt/kontain/runtime/libc.so")

        self.ssh_execute(
            remote_ip, "cd crun; ./autogen.sh && ./configure --disable-systemd && make all")
        self.ssh_execute(
            remote_ip, "cd crun; ln -s crun krun")
        self.ssh_execute(
            remote_ip, "sudo chmod 666 /dev/kvm")
        self.ssh_execute(
            remote_ip, "cd crun; OCI_RUNTIME=~/crun/crun make check-TESTS")
        self.ssh_execute(
            remote_ip, "cd crun; OCI_RUNTIME=~/crun/krun make check-TESTS")


def main():
    """ main """
    logging.basicConfig(
        format='%(asctime)s %(levelname)s %(message)s',
        level=logging.INFO,
        datefmt='%Y-%m-%d %H:%M:%S')

    parser = argparse.ArgumentParser()
    parser.add_argument("--no_cleanup_on_error",
                        help="version of km to be tested")
    parser.add_argument(
        "--test_id", help="a unique id used to create test resources")
    args = parser.parse_args()

    test_id = args.test_id
    if test_id is None:
        test_id = get_random_id()
    resource_group = f"{RESOURCE_GROUP}-{test_id}"

    remote_test = CRUNRemoteTest(
        resource_group,
        RESOURCE_GROUP_LOCATION,
        TESTING_VM_NAME,
        TESTING_VM_IMAGE,
        TESTING_VM_SIZE,
        TESTING_VM_ADMIN
    )

    cleanup_on_error = not args.no_cleanup_on_error
    remote_test.run(cleanup_on_error=cleanup_on_error)


main()
