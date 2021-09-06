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
#  Test KM using packer build process on AWS

variable "src_branch" {
  type        = string
  description = "Branch being tested"
}

variable "image_version" {
  type        = string
  description = "Branch-specific tag (aka IMAGE_VERSION) for docker images"
}

variable "target" {
  type        = string
  description = "which target to run See km-test.sh for details"
  default     = "test"
}

variable "dir" {
  type        = string
  description = "where to do 'make $target', e.g. tests or payloads"
}

variable "timeout" {
  type        = string
  description = "Timeout for tests. Should be less that outer timeout, so packer cleans up resources"
}

variables {
  aws_instance_type = "m5.xlarge" // 4 CPU 16GB
  aws_region        = "us-east-2"
  hv_device         = "/dev/kkm"
  ssh_user          = "fedora"
  ssh_group         = "fedora"
  // Azure access (for docker images)
  sp_tenant    = env("SP_TENANT")
  sp_appid     = env("SP_APPID")
  sp_password  = env("SP_PASSWORD")
  github_token = env("GITHUB_TOKEN")
}

data "amazon-ami" "build" {
  filters = {
    name                = "srini-test-image-ami"
    root-device-type    = "ebs"
    virtualization-type = "hvm"
  }
  most_recent = true
  owners      = ["782340374253"]
  region      = var.aws_region
}

locals {
  source_ami = data.amazon-ami.build.id
}

source "amazon-ebs" "km-test" {
  skip_create_ami = true
  ami_name        = "does not matter"
  source_ami      = local.source_ami
  instance_type   = var.aws_instance_type
  region          = var.aws_region
  ssh_username    = var.ssh_user
  # this allows to handle tty output in tests
  ssh_pty = true
}

build {
  sources = ["amazon-ebs.km-test"]

  provisioner "file" {
    sources     = ["/tmp/km", "/tmp/krun"]
    destination = "/tmp/"
  }

  provisioner "shell" {
    # packer provisioners run as tmp 'packer' user.
    # For docker to run with no sudo, let's add it to 'docker' group and
    # later use 'sg' to run all as this group without re-login
    inline = ["sudo usermod -aG docker ${var.ssh_user}"]
  }

  provisioner "shell" {
    script = "packer/scripts/km-test.sh"
    // double sg invocation to get docker into the process's grouplist but not the primary group.
    execute_command = "chmod +x {{ .Path }}; {{ .Vars }} sg docker -c 'sg ${var.ssh_group} {{ .Path }}'"

    // vars to pass to the remote script
    environment_vars = [
      "TRACE=1",
      "SRC_BRANCH=${var.src_branch}",
      "IMAGE_VERSION=${var.image_version}",
      "GITHUB_TOKEN=${var.github_token}",
      "HYPERVISOR_DEVICE=${var.hv_device}",
      "TARGET=${var.target}",
      "LOCATION=${var.dir}",
      "SP_APPID=${var.sp_appid}",
      "SP_PASSWORD=${var.sp_password}",
      "SP_TENANT=${var.sp_tenant}"
    ]
    timeout = var.timeout
  }

  error-cleanup-provisioner "shell" {
    script = "packer/scripts/gather-logs.sh"
  }
}
