# Build VMs with KM and KKM installed

Provides HashiCorp Packer templates and vagrant `Vagrantfile` to build VM images for different  targets (e.g. AWS, Azure, Vagrant, etc).

Assumes:

1. Packer & Vagrant are installed (see https://learn.hashicorp.com/tutorials)
2. For Packer: AWS access is configured in ~/.aws credentials file (https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html)
3. For Vagrant: VirtualBox is installed (https://www.virtualbox.org/)

**Warning** whan a vagrant/virtualbox VM is running on a host (i.e. after `vagrant up`) , KM (or QEMU) on the same host will fail with 'EBUSY', This is because VirtualBox uses it's own kernel module which takes exclusive ownership of CPU virtualization facility in Linux kernel, and thus /dev/kvm is not avaiable for monitors other than VirtualBox itself.

* To build **vagrant VM with KKM and KM installed**: `vagrant up`
  * Env. var `KM_VAGRANT_PRELOAD_IMAGES` can be used to force pre-pulling docker images
  * Env var `KM_VAGRANT_KM_RELEASE` can be used to force a specific KM release (instead of default) to be preinstalled
* To build **AMI with KKM and KM installed**: `make`. Note that this will also build kkm.run in top-level KM
* To **create AWS instances based on the above AMI** `make run-instance`

Example: bring up a fresh Vagrant VM (with VirtualBox provider), with with pre-installed KM release `v0.1-beta2-demo` and pre-pulled docker images; and then add to ssh config as `km-b2-demo`.

Dependencies:

* kkm.run needs to be pre-built (`make kkm-pkg` in KM top) and is expected in ../../build/kkm.run
* daemon.json in current dir needs to exist
* desired version on KM should be on km-releases github

```sh
export KM_VAGRANT_PRELOAD_IMAGES="kontainapp/spring-boot-demo kontainapp/runenv-python kontainapp/runenv-node"
export KM_VAGRANT_KM_RELEASE=v0.1-beta2-demo
vagrant up --provision
vagrant ssh-config --host km-b2-demo >> ~/.ssh/config

ssh km-b2-demo  # same as `cd tools/hashicorp; vagrant ssh`
```
