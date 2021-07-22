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

variable "km_build" {
   type        = string
   description = "Location of KM build"
   default     = "../../build"
}

variable "target_tmp" {
   type        = string
   description = "Location for transferred files on target VM"
   // This dir has to exist, and trailing '/' is important
   default     = "/tmp/"
}

// OS specific vars. Values need to be provided to packer via -var-file=images/<file>.pkrvars.hcl
variable "os" {
   type        = string
   description = "Base OS type for images. Used to locate correct install script"
}

variable "os_name" {
   type        = string
   description = "Image OS name. Defines directory name and name in the cloud"
}

variable "box_name" {
   type        = string
   description = "Base OS Vagrant box name"
}

variable "box_version" {
   type        = string
   description = "Vagrant source box version, for virtualbox provider"
}

locals {
   output_dir = "output_box_${var.os_name}"
}

source "vagrant" "kkm-box" {
   source_path = var.box_name
   box_version = var.box_version
   output_dir  = local.output_dir
   provider    = "virtualbox"
   teardown_method = "destroy"
   communicator    = "ssh"
   ssh_pty         = true
}

build {
   sources = ["vagrant.kkm-box"]

   provisioner "file" {
      sources     = ["${var.km_build}/kontain.tar.gz", "${var.km_build}/kkm.run", "daemon.json"]
      destination = var.target_tmp
   }

   provisioner "shell" {
      execute_command = "echo 'vagrant' | {{ .Vars }} sudo -S -E sh -eux '{{ .Path }}'"
      script          = "scripts/${var.os}.install_km.sh"
   }
}
