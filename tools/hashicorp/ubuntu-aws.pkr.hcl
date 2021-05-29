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
  type    = string
  description = "Location of KM build on build box"
  default = "../../build"
}

variable "km_release" {
  type    = string
  description = "Just a label for release"
  default = "beta3-kkm"
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
   target_tmp = "/tmp/"
   target_ami_label = "${var.os} ${var.os_version} with Kontain ${var.km_release}"
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
    OS_Version    = var.os
    Release       = var.os_version
    Timestamp     = local.timestamp
  }
}

build {
  sources = ["source.amazon-ebs.build"]

  provisioner "file" {
    sources     = ["${var.km_build}/kontain.tar.gz", "${var.km_build}/kkm.run", "daemon.json"]
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