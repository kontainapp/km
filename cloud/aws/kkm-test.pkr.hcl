//  Test KKM on AWS.

variable "source_branch" {
   type = string
   description="Branch being tested"
   default = env("SOURCE_BRANCH")
}

variables {
   aws_region = "us-east-2"
   sp_tenant = env("SP_TENANT")
   sp_appid = env("SP_APPID")
   sp_password = env("SP_PASSWORD")
   ssh_user = "fedora"
   test_script="kkm-test.bash"
}

source "amazon-ebs" "kkm-test" {
  // TODO - pass AMI id from outside
  source_ami      = "ami-00de22cbb5911d5c4"
  skip_create_ami = true
  ami_name        = "does not matter"
  instance_type   = "m5.xlarge"
  region          = var.aws_region
  ssh_pty         = true
  ssh_username    = var.ssh_user
}

build {
  sources = ["source.amazon-ebs.kkm-test"]

  provisioner "shell" {
    script = "${var.test_script}"
    environment_vars = [ // vars to pass to the remote script
      "TRACE=1",
      "SOURCE_BRANCH=${var.source_branch}",
      "SP_APPID=${var.sp_appid}",
      "SP_PASSWORD=${var.sp_password}",
      "SP_TENANT=${var.sp_tenant}"
    ]
  }
}
