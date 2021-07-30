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
// Create base image for vagrant/virtualbox operations
// Ubuntu image with vagrant/virtualbox preinstalled  (takes 15+ min)

variables {
   sp_tenant = env("SP_TENANT")
   sp_appid = env("SP_APPID")
   sp_password = env("SP_PASSWORD")

   image_name = "L0BaseImage"
   // RG should be created ahead of time
   image_rg = "PackerBuildRG"
}

source "azure-arm" "baseImage-for-CI" {
   subscription_id="bd3e8581-9352-4514-8e09-cf9b771b2155"
   tenant_id = var.sp_tenant
   client_id = var.sp_appid
   client_secret = var.sp_password

   ssh_username = "kontain"

   location = "West US"
   vm_size = "Standard_D4s_v3"

   // Base image info
   os_type = "Linux"
   image_publisher="Canonical"
   image_offer = "0001-com-ubuntu-server-focal"
   image_sku = "20_04-lts-gen2"

   // target image info. L0 is the bottom base image we use.
   // Other base images grow from it, if needed
   managed_image_name = var.image_name
   managed_image_resource_group_name = var.image_rg

  azure_tags = {
    reason = "Base Image for KM CI, with Packer and gcc and goodies"
  }
   ssh_pty = true
}

build {
  sources = ["sources.azure-arm.baseImage-for-CI"]

   provisioner "shell" {
      script = "scripts/L0-image-provision.sh"
   }

   // TBD azure Image should be "deprovisioned", see https://www.packer.io/docs/builders/azure/arm
   // see https://docs.microsoft.com/en-us/azure/virtual-machines/extensions/agent-linux
   // turned off for now as it breaks the build
   provisioner "shell" {
      execute_command = "chmod +x {{ .Path }}; {{ .Vars }} sudo -E sh '{{ .Path }}'"
      inline = [
         "echo SKIPPING /usr/sbin/waagent -force -deprovision+user && export HISTSIZE=0 && sync"
      ]
      inline_shebang = "/bin/sh -x"
   }
}
