//  build AMI with KM/KKM
// the right way would be to get Amazon Import post-processor in the buld_km_images;
// but amazon is  sensitive to specific kernel version so for now we will use this one as a workaround

variable "os" {
  type    = string
  default = "ubuntu"
}

variable "aws_region" {
  type    = string
  default = "us-west-1"
}


variable "km_release_version" {
  type    = string
  default = "v0.1-beta"
}

variable "km_build" {
  type    = string
  description = "Location of KM build"
  default = "../../build"
}

variable "target_tmp" {
  type    = string
  description = "Location for transferred files on target VM"
  // trailing / is important. Also, if the dir does not exist,
  // add shell provisioner with mkdir (non-root)
  default = "/tmp/"
}

variable "ssh_user" {
  type    = string
  default = "ubuntu"
}

variable "summary_manifest" {
  type    = string
  default = "packer-manifest.json"
}

variable "target_ami_label" {
  type    = string
  default = "Ubuntu 20.04 LTS with Kontain beta3/kkm"
}

variable "target_ami_name" {
  type    = string
  default = "Kontain_ubuntu_20.04"
}

data "amazon-ami" "build" {
  filters = {
    name                = "ubuntu/images/hvm-ssd/ubuntu-focal-20.04-amd64-server-20210129"
    root-device-type    = "ebs"
    virtualization-type = "hvm"
  }
  most_recent = true
  owners      = ["099720109477"]
  region      = "${var.aws_region}"
}

locals { timestamp = regex_replace(timestamp(), "[- TZ:]", "") }

source "amazon-ebs" "build" {
  ami_description             = var.target_ami_label
  ami_groups                  = ["all"]
  ami_name                    = var.target_ami_name
  associate_public_ip_address = true
  force_deregister            = true
  instance_type               = "t2.micro"
  region                      = var.aws_region
  source_ami                  = data.amazon-ami.build.id
  ssh_pty                     = true
  ssh_username                = var.ssh_user
  tags = {
    Base_AMI_Name = "{{ .SourceAMIName }}"
    Name          = var.target_ami_label
    OS_Version    = "Ubuntu"
    Release       = "20.04"
    Timestamp     = local.timestamp
  }
}

build {
  sources = ["source.amazon-ebs.build"]

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

}
