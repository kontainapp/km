#
# Copyright 2022 Kontain Inc
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
// Create base image for EC2 tests

variables {
  aws_access_key    = env("AWS_ACCESS_KEY_ID")
  aws_secret_key    = env("AWS_SECRET_ACCESS_KEY")
  ssh_user          = "ubuntu"
  aws_instance_type = "m5.xlarge" // 4 CPU 16GB

  volume_size       = 32
  aws_region        = "us-east-2"
  image_name = "L0BaseImage"
  username   = "kontain"
}

data "amazon-ami" "build" {
  filters = {
    name                = "ubuntu/images/hvm-ssd/ubuntu-focal-20.04-amd64-server-*"
    root-device-type    = "ebs"
    virtualization-type = "hvm"
  }
  owners      = ["099720109477"]
  most_recent = true
  region      = var.aws_region
}

source "amazon-ebs" "baseImage-for-CI" {
  access_key    = var.aws_access_key
  secret_key    = var.aws_secret_key
  region        = var.aws_region
  ami_name      = var.image_name
  source_ami    = data.amazon-ami.build.id
  instance_type = var.aws_instance_type
  ssh_username  = var.ssh_user
  launch_block_device_mappings {
    device_name           = "/dev/sda1"
    delete_on_termination = true
    volume_size           = var.volume_size
  }
}

build {
  sources = ["amazon-ebs.baseImage-for-CI"]

  provisioner "shell" {
    execute_command = "{{ .Vars }} sudo -E sh '{{ .Path }}' ${var.username}"
    script          = "../scripts/L0-image-provision.sh"
  }
}
