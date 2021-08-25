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
//  build AMI with KM/KKM
// the right way would be to get Amazon Import post-processor in the build_km_images;
// but amazon is  sensitive to specific kernel version so for now we will use this one as a workaround

variable "os" {
  type    = string
  default = "ubuntu"
}

variable "os_version" {
  type    = string
  default = "20.04"
}

variable "aws_region" {
  type    = string
  default = "us-west-1"
}

variable "km_build" {
  type        = string
  description = "Location of KM build on build box"
  default     = "../../build"
}

variable "km_label" {
  type        = string
  description = "Just a label for release"
  default     = "beta3-kkm"
}

variable "release_tag" {
  type        = string
  description = "Kontain release version"
}

variable "ssh_user" {
  type    = string
  default = "ubuntu"
}

variable "ssh_password" {
  // this is well know password for Vagrant boxes
  type    = string
  default = "vagrant"
}

locals {
  // Location for transferred files on target VM
  // trailing / is important. Also, if the dir does not exist,
  // add shell provisioner with mkdir (non-root)
  target_tmp       = "/tmp/"
  target_ami_label = "${var.os} ${var.os_version} with Kontain ${var.km_label}"
  target_ami_name  = "Kontain_${var.os}_${var.os_version}"
}

data "amazon-ami" "build" {
  filters = {
    name                = "ubuntu/images/hvm-ssd/ubuntu-*-${var.os_version}-amd64-server-*"
    root-device-type    = "ebs"
    virtualization-type = "hvm"
  }
  most_recent = true
  owners      = ["099720109477"]
  region      = var.aws_region
}

locals { timestamp = regex_replace(timestamp(), "[- TZ:]", "") }

source "amazon-ebs" "build" {
  ami_description             = local.target_ami_label
  ami_groups                  = ["all"]
  ami_name                    = local.target_ami_name
  associate_public_ip_address = true
  force_deregister            = true
  instance_type               = "t2.micro"
  region                      = var.aws_region
  source_ami                  = data.amazon-ami.build.id
  ssh_pty                     = true
  ssh_username                = var.ssh_user
  tags = {
    Base_AMI_Name = "{{ .SourceAMIName }}"
    Name          = local.target_ami_label
    Release       = var.release_tag
    Timestamp     = local.timestamp
  }
}

build {
  sources = ["source.amazon-ebs.build"]

  provisioner "file" {
    sources     = ["${var.km_build}/kontain.tar.gz", "${var.km_build}/kkm.run"]
    destination = local.target_tmp
  }

  provisioner "shell" {
    inline = [
      "echo ===== Waiting for cloud-init to complete...",
      "if [ -x /usr/bin/cloud-init ] ; then eval 'echo ${var.ssh_password} | sudo /usr/bin/cloud-init status --wait'; fi"
    ]
  }

  provisioner "shell" {
    execute_command = "echo '${var.ssh_password}' | {{ .Vars }} sudo -S -E sh -eux '{{ .Path }}'"
    script          = "scripts/${var.os}.install_km.sh"
  }
}
