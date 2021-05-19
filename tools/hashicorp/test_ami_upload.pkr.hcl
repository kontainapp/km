// This is an example of how to configure and test Packer Amazon Import post-processor
//
// Note that we do not use the Amazon post-processor so this file is FFU and reference
//
// It takes pre-existing OVA (passed in as `ova_name' variable and feeds it into AMI import
//
// AMI import assumes:
// 1.  ~/.aws/credentials (or other AWS credentials) properly set. See
//                 https://www.packer.io/docs/builders/amazon#specifying-amazon-credentials
// 2. an S3 bucket is available
// 3. vmimport role is confugured properly, including access to the above bucket. See
//                https://docs.aws.amazon.com/vm-import/latest/userguide/vmimport-image-import.html
//
// invoked by 'packer build <this file name>'

packer {
  required_plugins {
    amazon = {
      version = ">= 0.0.1"
      source = "github.com/hashicorp/amazon"
    }
  }
}

variable "ova_name" {
   type = string
   description = "name of OVA file to upload as AMI"
   default = "output_ova_ubuntu2010-kkm/ubuntu2010-kkm.ova"
}

locals {
   timestamp = regex_replace(timestamp(), "[- TZ:]", "")
}

source "null" "test-ami" {
  communicator = "none"
}

build {
  sources = ["sources.null.test-ami"]

  post-processors {
      post-processor  "artifice" {
         files = [var.ova_name]
      }

      post-processor "amazon-import" {
         keep_input_artifact = true
         license_type   = "BYOL"
         region         = "us-east-2"
         s3_bucket_name = "kontain-packer-ami-staging" // should exist in the above region
         ami_groups = ["all"]
         tags = {
            Description = "packer amazon-import ${local.timestamp}"
         }
      }
   }
}
