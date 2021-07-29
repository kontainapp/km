# Build VMs with KM and KKM installed

Provides HashiCorp Packer templates to build VM images with pre-installed KM/KKM, and then registers them locally and/or uploads to the cloud.

Assumes:

1. Packer & Vagrant are installed (see https://learn.hashicorp.com/tutorials)
2. For AMI creation: **AWS access is configured in ~/.aws credentials file (https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html)** and

**Warning** when a vagrant/virtualbox VM is running on a host (i.e. after `vagrant up`), KM (or QEMU) on the same host will fail with 'EBUSY'. This is because VirtualBox uses it's own kernel module which takes exclusive ownership of CPU virtualization facility, which disables KVM.

## Operations

Create/register/upload vm images:

1. Create VM images and Vagrant box files with pre-installed Kontain (`make vm-images`).
1. Register with local Vagrant (`make register-boxes`)
1. Upload to Vagrant cloud (`make upload-boxes`). This requires env VAGRANT_CLOUD_TOKEN
1. Create an AMI (`make ami`)

Use Vagrant boxes

1. `vagrant init kontain/ubuntu-kkm-beta3` or `vagrant init kontain/fedora-kkm-beta3`
1. vagrant up

==== END OF DOCUMENT ====
