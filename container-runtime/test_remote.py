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
TESTING_VM_NAME = f"kontain-crun-testing-vm"
# from: `az vm image list --all --offer fedora`
TESTING_VM_IMAGE = "Canonical:UbuntuServer:18.04-LTS:latest"
TESTING_VM_SIZE = "Standard_D2s_v3"
TESTING_VM_ADMIN = "kontain"

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
        self.logger.info(f"Init tests. RG {resource_group} at {resource_group_location}, vm {vm_name}")

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
        result = subprocess.run(ssh_execute_cmd, check=True)
        if result.returncode != 0:
            raise Exception(f"ssh execute failed. stderr: {result.stderr}")

        self.logger.info("Command successful: %s", cmd)

    def scp_to_remote(self, remote_ip, local, remote):
        """ scp_to_remote """
        self.logger.info(f"Copy -r {local} to {remote_ip}://{remote}")

        result = subprocess.run([
            "scp",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-r",
            local,
            f"{self.vm_admin}@{remote_ip}:{remote}"
        ], check=True)
        if result.returncode != 0:
           raise Exception(f"Copy failed. stderr: {result.stderr}")

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
                    "Failed ssh execute of /bin/true... Retry %d out of %d", run + 1, max_retry)
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
        self.ssh_execute(remote_ip, "/usr/bin/cloud-init status --wait")

        # Install stuff. Going forward we should put together an AMI with all that and skip this step
        tools_to_install = "make git gcc build-essential pkgconf libtool libsystemd-dev libcap-dev libseccomp-dev libyajl-dev libtool autoconf python3 automake libssl-dev"
        self.ssh_execute(remote_ip, "sudo apt-get update -q")
        self.ssh_execute(remote_ip, f"sudo apt-get install -q -y {tools_to_install}")

        # crun build needs to update it's own submodules, but the target machine
        # won't have git repo (which is in ../../km/.git/modules), so let's make sure the modules are
        # updated before copy. Also, crun build will say "fatal: not a git repository" which could be ignored
        result = subprocess.run("cd crun && git submodule update --init --recursive", shell=True, capture_output=True)
        if result.returncode != 0:
            raise Exception(f"Failed to update crun submodules. out: '{result.stdout}'' err: '{result.stderr}'")

        self.scp_to_remote(remote_ip, "crun", "~/")
        self.scp_to_remote(remote_ip, "/opt/kontain/bin/km", "~/km")
        self.ssh_execute(
            remote_ip, "sudo mkdir -p /opt/kontain/bin; sudo mv ~/km /opt/kontain/bin/km")

        self.ssh_execute(
            remote_ip, f"cd crun; ./autogen.sh && ./configure --disable-systemd && make all")
        self.ssh_execute(
            remote_ip, f"cd crun; ln -s crun krun")
        self.ssh_execute(
            remote_ip, "sudo chmod 666 /dev/kvm")
        self.ssh_execute(
            remote_ip, f"cd crun; OCI_RUNTIME=~/crun/crun make check-TESTS")
        self.ssh_execute(
            remote_ip, f"cd crun; OCI_RUNTIME=~/crun/krun make check-TESTS")


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
        test_id = str(uuid.uuid4())[:8] # get random id if not passed
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
