

# install dependencies

Each kernel version has its own set of dependencies.

On fedora

```bash
dnf config-manager --set-enabled fedora-source
dnf download --source kernel
rpm -ivh <kernel-rpm-name>
dnf builddep ${HOME}/rpmbuild/SPECS/kernel.spec
```

# build kkm module

From km root directory

```bash
make -C kkm/kkm
```
This will generate module kkm/kkm/kkm.ko

# install module

Use this script.

```bash
#!/bin/bash

sudo -u root /bin/bash -x << EOF

rmmod kvm_intel
rmmod kvm
rmmod kkm

dmesg -c > /dev/null
# change to the location of your source repository.
insmod /data/km/kkm/kkm/kkm.ko

lsmod | grep -e kvm -e kkm

EOF
```

# running tests

Typically when the above script is used and only kkm is present in kernel, tests can be run as normal.

```bash
make -C tests test
```

When both kvm and kkm are present in the kernel, tests can be run with kkm module using this command

```bash
make -C tests test USEVIRT=kkm
```

# installing virtual machine to isolate development environment


https://docs.fedoraproject.org/en-US/quick-docs/getting-started-with-virtualization/