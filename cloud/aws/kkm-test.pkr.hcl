//  Test KKM on AWS.

variable "source_branch" {
   type = string
   description="Branch being tested"
}

variable "image_version" {
   type = string
   description="Branch-specific tag (aka IMAGE_VERSION) for docker images"
}

// TODO - pass AMI id from outside
variable "aws_ami_id" {
   type = string
   description="AMI with pr-reqs preinstalled"
   default = "ami-00de22cbb5911d5c4"
}

variables {
   test_script="kkm-test.bash"
   aws_instance_type = "m5.2xlarge" # 8 CPU 32GB
   aws_region = "us-east-2"
   ssh_user = "fedora"
   // Azure access (for docker images)
   sp_tenant = env("SP_TENANT")
   sp_appid = env("SP_APPID")
   sp_password = env("SP_PASSWORD")
}

source "amazon-ebs" "kkm-test" {
  skip_create_ami = true
  ami_name        = "does not matter"
  source_ami      = var.aws_ami_id
  instance_type   = var.aws_instance_type
  region          = var.aws_region
  ssh_pty         = true
  ssh_username    = var.ssh_user
}

build {
  sources = ["source.amazon-ebs.kkm-test"]

  provisioner "shell" {
    script = "${var.test_script}"
    environment_vars = [ // vars to pass to the remote script
      // "TRACE=1",
      "SOURCE_BRANCH=${var.source_branch}",
      "IMAGE_VERSION=${var.image_version}",
      "SP_APPID=${var.sp_appid}",
      "SP_PASSWORD=${var.sp_password}",
      "SP_TENANT=${var.sp_tenant}"
    ]
  }
}
