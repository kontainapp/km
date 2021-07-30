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
//  Test KM using packer build process

variable "src_branch" {
   type = string
   description="Branch being tested"
}

variable "image_version" {
   type = string
   description="Branch-specific tag (aka IMAGE_VERSION) for docker images"
}

variable "target" {
   type = string
   description="which target to run See km-test.sh for details"
   default="test"
}

variable "dir" {
   type = string
   description="where to do 'make $target', e.g. tests or payloads"
}

variable "hv_device"  {
   type = string
   description = "/dev/kvm or /dev/kkm"
}
variable "timeout" {
   type = string
   description = "Timeout for tests. Should be less that outer timeout, so packer cleans up resources"
}

// TODO - pass AMI id from outside
variable "aws_ami_id" {
   type = string
   description="Pre-built AMI with all needes stuff installed"
   default = "ami-00de22cbb5911d5c4" // TODO - use AMI data and find AMI by name
}

variables {
   aws_instance_type = "m5.xlarge"  // 4 CPU 16GB
   aws_region = "us-east-2"
   ssh_user = "fedora"
   // Azure access (for docker images)
   sp_tenant = env("SP_TENANT")
   sp_appid = env("SP_APPID")
   sp_password = env("SP_PASSWORD")
   github_token = env("GITHUB_TOKEN")
   // azure images
   src_image_name = "L0BaseImage" // would be good to pass from upstairs so others can use it
   // can conflict with other runs. TODO - make unique
   // see https://www.packer.io/docs/templates/hcl_templates/functions
   image_name = "KKMTestTmpImage"
   image_rg = "PackerBuildRG"
}

locals {
   # easily identify packer resource groups in Azure. E.g. "pkr-km-test-ci-251-payloads-busybox"
   az_tmp_resource_group = "pkr-km-test-${replace(trim(var.dir, "/"), "/", "-")}-${var.image_version}"
}

source "amazon-ebs" "km-test" {
  skip_create_ami = true
  ami_name        = "does not matter"
  source_ami      = var.aws_ami_id
  instance_type   = var.aws_instance_type
  region          = var.aws_region
  ssh_username    = var.ssh_user
  # this allows to handle tty output in tests
  ssh_pty         = true
}

source "azure-arm" "km-test" {
   subscription_id="bd3e8581-9352-4514-8e09-cf9b771b2155"
   tenant_id = var.sp_tenant
   client_id = var.sp_appid
   client_secret = var.sp_password

   location = "West US"
   temp_resource_group_name = local.az_tmp_resource_group
   # this one allow KVM and KKM. For KKM only, a smaller size would be cheaper
   vm_size = "Standard_D4s_v3"

   // Base image info
   os_type = "Linux"
   custom_managed_image_name = var.src_image_name
   custom_managed_image_resource_group_name = var.image_rg

   // target image
   // TODO - add job_id to box so there are no conflict for multiple CIs
   managed_image_name = var.image_name
   managed_image_resource_group_name = var.image_rg

  azure_tags = {
    reason = "Test KM using Packer on Azure"
  }
  # this allows to handle tty output in tests
  ssh_pty         = true
}

build {
//   sources = ["amazon-ebs.km-test", "azure-arm.km-test"]
  sources = ["amazon-ebs.km-test"]

  provisioner "shell" {
    # packer provisioners run as tmp 'packer' user.
    # For docker to run with no sudo, let's add it to 'docker' group and
    # later use 'sg' to run all as this group without re-login
    inline = ["sudo usermod -aG docker $USER" ]
  }

  provisioner "shell" {
    script = "packer/scripts/km-test.sh"
    execute_command = "chmod +x {{ .Path }}; {{ .Vars }} sg docker -c '{{ .Path }}'"
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


  post-processor "shell-local" {
    // assumes 'az login' on the box running packer
    only = ["azure-arm.km-test"]

    inline = [
       "echo Deleting image ${var.image_name} in ${var.image_rg}",
       "az login --service-principal -u ${var.sp_appid} -p ${var.sp_password} --tenant ${var.sp_tenant} -o table",
       "az image delete -n ${var.image_name} -g ${var.image_rg}"
      ]
  }
}
