# Build VMs with KM and KKM installed

Provides HashiCorp Packer templates and vagrant `Vagrantfile` to build VM images for different targets (e.g. AWS, Azure, Vagrant, etc).

Assumes:

1. Packer & Vagrant are installed (see https://learn.hashicorp.com/tutorials)
2. For Packer: AWS access is configured in ~/.aws credentials file (https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html)
3. For Vagrant: VirtualBox is installed (https://www.virtualbox.org/)

**Warning** when a vagrant/virtualbox VM is running on a host (i.e. after `vagrant up`), KM (or QEMU) on the same host will fail with 'EBUSY', This is because VirtualBox uses it's own kernel module which takes exclusive ownership of CPU virtualization facility in Linux kernel, and thus /dev/kvm is not available for monitors other than VirtualBox itself.

## NEW Process

1. [optional] Create a box with pre-installed Kontain (`make vm-image` for create)
2. `vagrant init kontain/kkm-beta-OS`
2. vagrant up

---- **FURTHER TEXT NEED CLEANUP**
## OLD Process

* To build **vagrant VM with KKM and KM installed**: `vagrant up`
  * Env. var `KM_VAGRANT_PRELOAD_IMAGES` can be used to force pre-pulling docker images
  * Env var `KM_VAGRANT_KM_RELEASE` can be used to force a specific KM release (instead of default) to be pre-installed
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

ssh km-b2-demo # same as `cd tools/hashicorp; vagrant ssh`
```

## Upload Vagrant box to vagrantcloud

There are two boxes defined here, Ubuntu based and Fedora based, in `ubuntu` and `fedora` directories correspondingly.

When a user creates a VM with `vagrant init kontain-box-name; vagrant up`, the kontain box will be downloaded from vagrantcloud.

To place our box there, first we create a VM image ("box" in vagrantspeak):

```bash
cd ubuntu | fedora; vagrant up [ --provider virtualbox ]; vagrant halt; vagrant package --output /tmp/beta2-kkm.box`
```

### Adding the new box locally

To avoid roundtrip to and from vagrantcloud you can add the box locally

```bash
vagrant box add --name kontain-box-name /tmp/beta2-kkm.box
```

To upload the box to vagrantcloud login to vagrantcloud and upload the box.
Note that the box name should be `kontain/beta2-kkm` (it is mentioned in km-releases doc so change it there if needed),
and provider should be virtualbox.

vagrantcloud account: for now we have an account `kontain` with the same password as our wireless password. ("SecureFxx...")

To update:

* run md5sum on the .box file created above, on your dev machine
* Login to vagrantclound.com and click in the box name (`kontain/beta2-kkm`)
* Click "new version"
* add a provider (virtualbox) with the proper checksum type/value (md5/result of md5sum above)
* upload the .box file
* Click "release" for the new version

When all is uploaded, you can remove the local .box file, or you can register it with vagrant with "vagrant box add" command (see manual).

Note: the above is a basic guide. I am sure we'll have more boxes if needed, so just capturing basic steps. These steps will be automated if we will get into the business of providing vagrant boxes.
