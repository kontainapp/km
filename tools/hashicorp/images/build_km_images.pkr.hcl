//
// gets pre-loaded Vagrant Box, installs KKM in virtualbox Builder.
// and then uploads to VagrantCloud (as a box) and AWS (as export AMI)
//

// generic vars

variable "home" {
   type    = string
   description = "User HOME. Need to locate vagrant box storage"
   default = "${env("HOME")}"
}

variable "km_build" {
   type    = string
   description = "Location of KM build"
   default = "../../build"
}

variable "target_tmp" {
   type    = string
   description = "Location for transferred files on target VM"
   // This dir has to exist, and trailing '/' is important
   default = "/tmp/"
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

variable "box_version" {
   type    = string
   description = "Vagrant source box version, for virtualbox provider. "
   // Note: the box with this version has to be already on local machine (see Makefile)"
}

variable "cloud_box_version" {
   type    = string
   description = "Box version as injected in VagrantCloud"
   // TODO: pass it from outside and coordinate with release version
   default = "0.1.11"
}

variable "cloud_token" {
   type    = string
   description = "Vagrant cloud access token"
   default = "${env("VAGRANT_CLOUD_TOKEN")}"
}

locals {
   // Base for boxes and OVA names, e.g. fedora32-kkm
   output_base = "${replace("${var.box_name}", "generic/", "")}-kkm"

   // box name (and local file with created box) e.g. box/fedora32-kkm.box
   output_box = "box/${local.output_base}.box"

   // box name on Vagrant Cloud. Box has to exist
   box_tag = "kontain/${var.os}-kkm-beta3"

   // Timestamt to add to descriptions
   timestamp = regex_replace(timestamp(), "[- TZ:]", "")

   // File name base in vagrant boxes storage, e.g. generic-VAGRANTSLASH-fedora32
   box_mangled_name = replace("${var.box_name}", "/", "-VAGRANTSLASH-")

   // Box path in local Vagrant boxes storage
   source_path = "${var.home}/.vagrant.d/boxes/${local.box_mangled_name}/${var.box_version}/virtualbox/box.ovf"
}

/// fake builder, used for post-processorts debugging only
// OVA file needs to be created on prior runs on the builder
source "file" "debug-pp" {
   source = "output_ova/${local.output_base}.ova"
   target = "output_ova/tmp-${local.output_base}.ova"
}

source "virtualbox-ovf" "build-ova" {
   source_path          = local.source_path
   output_directory     = "output_ova_${local.output_base}"
   output_filename      = local.output_base
   format               = "ova"
   guest_additions_mode = "disable"
   headless             = "true"
   ssh_password         = "vagrant"
   ssh_username         = "vagrant"
   ssh_wait_timeout     = "30s"
   shutdown_command     = "echo 'packer' | sudo -S shutdown -P now"
   vboxmanage = [
      ["modifyvm", "{{.Name}}", "--memory", "8182"],
      ["modifyvm", "{{.Name}}", "--cpus", "4"],
   ]
   export_opts = [
         "--vsys", "0",
         "--vendor", "Kontain, Inc",
         "--description", "VM based on ${var.box_name} with preinstalled Kontain",
         "--version", "0.1-Beta ${local.timestamp}"
      ]
}

build {
   sources = ["source.file.debug-pp", "source.virtualbox-ovf.build-ova"]

   provisioner "file" {
      sources     = ["${var.km_build}/kontain.tar.gz", "${var.km_build}/kkm.run", "daemon.json"]
      destination = var.target_tmp
   }

   provisioner "shell" {
      inline = [
         "echo ===== Waiting for cloud-init to complete...",
         "if [ -x /usr/bin/cloud-init ] ; then eval 'echo vagrant | sudo /usr/bin/cloud-init status --wait'; fi"
      ]
   }

   provisioner "shell" {
      execute_command = "echo 'vagrant' | {{ .Vars }} sudo -S -E sh -eux '{{ .Path }}'"
      script          = "scripts/${var.os}.install_km.sh"
   }

   post-processors {
      post-processor "vagrant" {
         output              = local.output_box
         keep_input_artifact = true
      }

      // TODO - not sure what to do here.
      post-processor "shell-local" {
         // delete the target box, no questions asked. No Parallel Runs here !
         inline = ["curl --silent --show-error --header 'Authorization: Bearer ${var.cloud_token}' --request DELETE  https://app.vagrantup.com/api/v1/box/${local.box_tag}/version/${var.cloud_box_version}"]
      }

      post-processor "vagrant-cloud" {
         box_tag      = local.box_tag  // this box must already exist
         version      = var.cloud_box_version
         access_token = var.cloud_token
         version_description = "${var.box_name} with preinstalled Kontain/KKM Beta. Time: ${local.timestamp}"
      }
   }

   post-processor "shell-local" {
      inline              = ["echo TODO: Register box with local Vagrant if needed "]
      keep_input_artifact =  true
   }

}
