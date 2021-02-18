# Build VMs with KM and KKM installed

Provides HashiCorp Packer templates and vagrant `Vagrantfile` to build VM images for different  targets (e.g. AWS, Azure, Vagrant, etc).

Assumes:

1. Packer & Vagrant are installed (see https://learn.hashicorp.com/tutorials)
2. For Packer: AWS access is configured in ~/.aws credentials file (https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html)
3. For Vagrant: VirtualBox is installed (https://www.virtualbox.org/)

**Warning** whan a vagrant/virtualbox VM is running on a host (i.e. after `vagrant up`) , KM (or QEMU) on the same host will fail with 'EBUSY', This is because VirtualBox uses it's own kernel module which takes exclusive ownership of CPU virtualization facility in Linux kernel, and thus /dev/kvm is not avaiable for monitors other than VirtualBox itself.

* To build vagrant VM with KKM and KM installed: `vagrant up`
  * To pre-load extra docker images, set env var KM_VAGRANT_PRELOAD_IMAGES, eg. `export KM_VAGRANT_PRELOAD_IMAGES="kontainapp/spring-boot-demo kontainapp/runenv-python kontainapp/runenv-node"`
* To build AMI wirh KKM and KM installed: `make`. Note that this will also build kkm.run in top-level KM
* To create AWS instances based on the above AMI `make run-instance`