// builds vagrant box with KKM installed based on ${var.os} box.

// TODO in this file:
// - drop ubuntu / fedora in file or dir name per OS
// - pick up the name in post-processor
// - clean up vars

variable "km_build" {
   type        = string
   description = "Location of KM build"
   default     = "../../build"
}

variable "box_version" {
   type    = string
   description = "Vagrant source box version, for virtualbox provider. "
}
variable "target_tmp" {
   type        = string
   description = "Location for transferred files on target VM"
   // This dir has to exist, and trailing '/' is important
   default     = "/tmp/"
}

// os-specific vars. Values need to be provided to packer via -var-file=images/<file>.pkrvars.hcl
variable "os" {
   type    = string
   description = "Base OS name for images. Used to locate correct install script"
}

variable "box_name" {
   type    = string
   description = "Base OS Vagrant box name"
}

locals {
   // Final box name -  local, and on Vagrant Cloud.
   box_tag = "kontain/${var.os}-kkm-beta3"
   output_dir = "output_vagrant_test"
}

source "vagrant" "kkm-box" {
   source_path = "generic/ubuntu2010"
   output_dir  = local.output_dir
   provider    = "virtualbox"
   teardown_method = "destroy"
   communicator = "ssh"
   ssh_pty = true

   vagrantfile_template = "kkm-box.vagrantfile"
}
/// fake builder, used for debugging post-processors, e.g.
//packer build --only null.debug  -var-file templates/ubuntu2010.pkrvars.hcl templates
source "null" "debug" {
   communicator = "none"
}

build {
   sources = ["vagrant.kkm-box", "null.debug"]

   provisioner "file" {
      except = ["null.debug"]
      sources     = ["${var.km_build}/kontain.tar.gz", "${var.km_build}/kkm.run", "daemon.json"]
      destination = var.target_tmp
   }
   provisioner "shell" {
      except = ["null.debug"]
      execute_command = "echo 'vagrant' | {{ .Vars }} sudo -S -E sh -eux '{{ .Path }}'"
      script          = "scripts/${var.os}.install_km.sh"
   }

   post-processor "shell-local" {
      inline = [
         "echo Registering box: vagrant box add -f ${local.box_tag} ${local.output_dir}/package.box",
         "vagrant box add -f ${local.box_tag} ${local.output_dir}/package.box"
         ]
   }
}
