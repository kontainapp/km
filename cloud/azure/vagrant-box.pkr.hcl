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
// Run Packer on Large AZ instance with nested virtualization.
// Use vagrant/virtualbox there to build Kontain Boxes (as defined in tools/hashicorp)
// Clean up VM image created - we only need steps (vagrant) not the image

variable "src_branch" {
   type = string
   description="Branch being tested"
   // default = "master"
}
variable "image_version" {
   type = string
   description="Build version/tag for docker images"
   default = "latest"
}

variables {
   // Build artifacts to transfer to the VM where we build vagrant boxes
   artifacts = [
      "../../build/kontain.tar.gz",
      "../../build/kkm.run",
      "../../tools/hashicorp/daemon.json"
   ]

   sp_tenant = env("SP_TENANT")
   sp_appid = env("SP_APPID")
   sp_password = env("SP_PASSWORD")

   // For runs from desktop, these should be personal tokens
   // For CI, there should be secrets
   github_token = env("GITHUB_TOKEN")
   vagrant_cloud_token = env("VAGRANT_CLOUD_TOKEN")

   image_name = "VagrantRun"
   // RG and base image should be created ahead of time
   src_image_name = "L0BaseImage"
   image_rg = "PackerBuildRG"

}

locals {
   // Location for transferred files on target VM. trailing / is important.
   target_tmp = "/tmp/"
}

source "azure-arm" "vagrant-boxes-build" {
   subscription_id="bd3e8581-9352-4514-8e09-cf9b771b2155"
   tenant_id = var.sp_tenant
   client_id = var.sp_appid
   client_secret = var.sp_password

   // Base image
   os_type = "Linux"
   custom_managed_image_name = var.src_image_name
   custom_managed_image_resource_group_name = var.image_rg

   // target image
   // TODO - add job_id to box so there are no conflict for multiple CIs
   managed_image_name = var.image_name
   managed_image_resource_group_name = var.image_rg

   location = "West US"
   vm_size = "Standard_D4s_v3"

  azure_tags = {
    reason = "Vagrant Box Building"
  }
   ssh_pty         = true
}

build {
  sources = ["sources.azure-arm.vagrant-boxes-build"]

   provisioner "file" {
      sources     = var.artifacts
      destination = local.target_tmp
   }

   provisioner "shell" {
   script =  "scripts/vagrant-box.sh"
   environment_vars = [
      "SRC_BRANCH=${var.src_branch}",
      "GITHUB_TOKEN=${var.github_token}",
      "VAGRANT_CLOUD_TOKEN=${var.vagrant_cloud_token}"
   ]
   }

   post-processor "shell-local" {
    // assumes 'az login' on the box running packer
    inline = [
       "echo Deleting image ${var.image_name} in ${var.image_rg}",
       "az image delete -n ${var.image_name} -g ${var.image_rg}"
      ]
  }
}
