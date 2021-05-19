// Test upload to VagrantCloud mechanism
// to run: packer build <this_file_name>

locals {
   box         = "box/ubuntu2010-kkm.box"   // Local box file name to upload to cloud
   box_tag     = "kontain/upload-test"      // Box to upload to - must already exist
   timestamp   = regex_replace(timestamp(), "[- TZ:]", "")
}

variable  "cloud_box_version"  {
   type    = string
   description = "Version in the cloud to uploade to, and publish"
   default = "0.0.1"
}

variable "cloud_token" {
  type    = string
  description = "Vagrant clound access token"
  default = "${env("VAGRANT_CLOUD_TOKEN")}"
}

source "null" "test-vagrant-upload" {
  communicator = "none"
}

build {
  sources = ["sources.null.test-vagrant-upload"]

  post-processors {
      post-processor "shell-local" {
         // delete the test box, no questions asked. No Parallel Runs here !
         inline = ["curl --silent --show-error --header 'Authorization: Bearer ${var.cloud_token}' --request DELETE  https://app.vagrantup.com/api/v1/box/${local.box_tag}/version/${var.cloud_box_version}"]
      }

      post-processor  "artifice" {
         files = [local.box]
      }

      post-processor "vagrant-cloud" {
         box_tag      = local.box_tag
         version      = var.cloud_box_version
         access_token = var.cloud_token
         version_description = "Test for uploading packer-built Box to Vagrant Cloud. Time: ${local.timestamp}"
      }
   }
}
