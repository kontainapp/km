# Working with VMs

This document describes how to build and run a virtual machine with Fedora guest OS installed,
ready to compile and run km and the payloads.

While it was tested on Fedora host OS, the host OS is largely inconsequential.
Other than initial prerequisites installation everything should work on other hosts.

We describe how to create and run a VM based on a pre-made base disk image.
This allows for a short time to km running inside a VM.
The base image was created circa Oct 8th, 2020,
using Fedora 32 server.

We also describe how to make the base image starting from empty disk and installation media,
so that it can be recreated as need with new versions of Fedora or different install configuration.

## Prerequisites

```bash
sudo dnf install qemu-system-x86 tigervnc pbzip2
```

## Quick start from preinstalled image

There is a VM image prepared with Fedora 32 server system, installed prerequisites and upgraded.
Make a directory for VM disk images.
Download the compressed image, decompress, and place in that directory

```bash
mkdir ~/VMs
cd ~/VMs
wget https://vdisks.blob.core.windows.net/fedora/fedora_server_32_base.qcow2.bz2
pbzip2 -d fedora_server_32_base.qcow2.bz2
```

This will place fedora_server_32_base.qcow2 in that directory.
You can use that file as VM disk directly.
However it is easier to keep the base and work with differencing disks,
using base as an immutable checkpoint / starting point.
Create differencing disk:

```bash
chmod a-w fedora_server_32_base.qcow2
qemu-img create -f qcow2 -o backing_file=~/VMs/fedora_server_32_base.qcow2 ~/VMs/f32_1.qcow2
```

There are two ports to connect to running VM, one for console and one for ssh.
We are going to use bash variables `$VM_CONSOLE` and `$VM_SSH` in the following scripts.
Also, `$VM_VCPUS` is virtual cpus count, `$VM_MEMORY` is memory in MB, and `$VM_DISK` is the disk.

```bash
VM_CONSOLE=5900
VM_SSH=5555
VM_VCPUS=8
VM_MEMORY=8192
VM_DISK=~/VMs/f32_1.qcow2
```

To start the VM:

```bash
nohup qemu-system-x86_64 -accel kvm -cpu host -smp $VM_VCPUS -m $VM_MEMORY \
   -vnc :`expr $VM_CONSOLE - 5900` -hda $VM_DISK -net user,hostfwd=::$VM_SSH-:22 -net nic &
```

If you want to run a VM with nested KVM disabled,
e.g. to test kkm an AWS like machine,
add `-vmx` to cpu specification:

```bash
nohup qemu-system-x86_64 -accel kvm -cpu host,-vmx -smp $VM_VCPUS -m $VM_MEMORY \
   -vnc :`expr $VM_CONSOLE - 5900` -hda $VM_DISK -net user,hostfwd=::$VM_SSH-:22 -net nic &
```

To connect to the running VM's console use vncviewer and connect to `:$VM_CONSOLE`.
This could be command line `vncviewer :$VM_CONSOLE` or via graphical dialog.

To connect to the shell use `ssh fedora@localhost -p $VM_SSH`.
Password is the same as the office WiFi password.
To simplify further connections copy your public ssh key into the VM:

```bash
cat ~/.ssh/id_rsa.pub | ssh fedora@localhost -p $VM_SSH 'cat >> .ssh/authorized_keys'
```

## Tricks for remote access

If running a VM on a remote machine,
like working from home accessing a machine in the office,
here are handy trick.

The examples below assume `kontain` is configured to ssh connect to the host machine,
i.e. in `~/.ssh/config` there is an entry like this:

```txt
      ...
   Host kontain
	HostName 73.241.134.96
	Port 2222
	User serge
	ForwardX11=yes
	ForwardX11Trusted=yes
      ...
```

To redirect ports to access the remote VM,
when connecting from home use something like:

```bash
ssh kontain -L $VM_CONSOLE:localhost:$VM_CONSOLE -L $VM_SSH:localhost:$VM_SSH
```

This will make it so connecting to the `$VM_CONSOLE` and `$VM_SSH` on the home machine
will be redirected to the same ports on the remote (`kontain` in this case).

If desirable to use different local port numbers:

```bash
ssh kontain -L <local_port_for_console>:localhost:$VM_CONSOLE -L <local_port_for_ssh>:localhost:$VM_SSH
```

In order to make it possible to run long term tests or other tasks,
it is handy to use `screen` program on the office machine.
Screen allows to keep the shell inside of it running while the outside one can be disconnected.

Login to the office machine `ssh kontain`, install `sudo dnf install screen`.
Then run `screen`, which will launch new shell.
From that shell you can connect to teh VM and run long term tests.
While the tests are running you can detach from screen typing `^a d`, and exit the expernal shell.
The internal shell and other processes keep running.

You can reconnect to the screen at a later time `screen -r`.

## How to prepare the base image

To prepare the base image we need to create an empty disk,
download install media `.iso`,
boot the VM from the install media,
and go through the installation process.
Using the same bash variables as above, here are the steps:

### Create an empty disk, start the VM

```bash
qemu-img create -f qcow2 $VM_DISK 80G
nohup qemu-system-x86_64 -accel kvm -cpu host -smp $VM_VCPUS -m $VM_MEMORY \
   -vnc :`expr $VM_CONSOLE - 5900` -hda $VM_DISK -net user,hostfwd=::$VM_SSH-:22 -net nic \
   -cdrom Fedora-Server-dvd-x86_64-32-1.6.iso
```

### Installation

Connect to the console using `vncviewer $VM_CONSOLE` and go through the installation process.
While more or less any installation configuration would work,
it is recommended to configure the storage to use standard partitions (rather than default LVM),
and only create a partition for `/` instead of using separate `/` and `/home`.
Also there is really no reason to use any swap,
so the swap partition could be made very small,
like `0.1GiB`.
It is recommended to install fewer packages to save disk space.

Once the installation is complete reboot the machine and follow configuration steps.
Create a user named `fedora`.
When this is complete login on the console
and follow steps in `build.md` document to prepare new Fedora machine.
Once you enable and start `sshd` you can connect from regular terminal window.

When the system is done to your liking run `sudo fstrim /` and shut down the system.
The disk file `$VM_DISK` will be your base image.
It might make sense to try to compress it (although it didn't help much),
and compress to minimize transfer time.

```bash
qemu-img convert $VM_DISK $VM_DISK_base.qcow2
chmod a-w $VM_DISK_base.qcow2
pbzip2 -m1024 -k $VM_DISK_base.qcow2
```

At this point we have the base image and ready to go from the top of this document.

### Upload

```bash
az storage blob upload --auth-mode login --account-name vdisks --container-name fedora \
   --name fedora_server_32_base.qcow2.bz2 --file /var//stuff/serge/VMs/fedora_32_base.qcow2.bz2
```