# Kontain Monitor, Kontain VM and unikernels

Public repository with Kontain binary releases. Kontain code is not open source currently and is maintained in a private repository.

Kontain is the way to run container workloads "Secure, Fast and Small - choose three". Kontain runs workloads in a dedicated VM, as a unikernel - within Kontain VM. A workload can be a regular Linux executable, or a Kontain "unikernel" - your code relinked with Kontain libraries to run directly on virtual hardware, without OS layer in the VM. Running in a Kontain Virtual Machine provides VM level isolation/security, but without any of VM overhead - in fact, Kontain workloads startup time is closer to that of Linux process and is faster than Docker containers.

Kontain seamlessly plugs into Docker or Kubernetes run time environments.

Kontain release includes Kontain Monitor, runtime libraries, tools and pre-build unikernel payloads, e.g. Python-3.7.

**Version:** 0.1-beta2

## Table of Contents

- [Status](#status)
- [Platform support](#platform-support)
- [Install](#install)
- [Background](#background)
- [Usage](#usage)
- [Debugging](#debugging)
- [Architecture](#architecture)
- [Cloud](#cloud)
- [FAQ](#faq)
- [Contributing](#contributing)
- [Licensing](#licensing)

## Platform support

- Kontain currently supports only **Linux** hosts with kernel 4.15 and above. We recommend `Fedora 32` or `Ubuntu 20`.
- Kontain needs access to virtualization - i.e. *KVM module* enabled, or Linux with *Kontain Kernel Module (KKM) installed and loaded*.
- `On AWS` non-metal instances, Kontain KKM kernel module is required, For demo purposes, we provide an AWS AMI of Ubuntu 20 with KKM installed and pre-loaded.
- `On GCP`, do not use default Linux VM. Default (as of 11/2020) provisioning of a linux VM uses Debian 9 distribution, which is based on 4.09 Kernel. Please choose other distributions with fresher kernel, e.g. Ubuntu 20 LTS.
- `Earlier Linux distributions` (e.g. Ubuntu 18 LTS) are not fully supported, mainly due to limited testing. If you need one of these please try first, and if there are issues please submit an issue for this repository.

## Install: use a pre-configured VM with all components pre-installed

The easiest way to get started with Kontain is to use a pre-configured VM with all components (including Kontain Kernel Module) installed.
Kontain provides a Vagrant Box with Ubuntu 20.10 and Kontain pre-installed.

Assuming you have [vagrant](https://www.vagrantup.com/) and [virtualbox](https://www.virtualbox.org/) installed (e.g. on Mac, `brew install vagrant virtualbox`), you can simply run `kontain/beta2-kkm` box from *vagrantcloud*:

```sh
mkdir try-kontain; cd try-kontain
vagrant init kontain/beta2-kkm
vagrant up --provider=virtualbox
vagrant ssh
```

Currently *kontain/beta2-kkm* box is provided for *virtualbox* provider only. Please submit an issue if you'd like to have a different provider.

Kontain also provide AWS AMI with Kontain pre-installed; it is documented further in this box

## Install: install pre-requisites and components manually

### Check Pre-requisites

If you want to give it a try on OSX or Windows, please create a Linux VM with nested
virtualization (/dev/kvm) available - e.g. VMWare Fusion on Mac supports it out of the box.

- To check Linux kernel version, use `uname -a`.
- To check that KVM is and kvm module is loaded use `lsmod | grep kvm` ; also validate that */dev/kvm* exists and has read/write permissions `ls -l /dev/kvm`.

#### Docker and gcc

For docker-related and language-related examples to work, docker and gcc (and Go) need to be installed. For Docker, use `apt-get` installation as described in https://docs.docker.com/engine/install/. **Ubuntu SNAP is not supported**

### Install Kontain files

Install script will install Kontain files, and then validate execution of a simple unikernel in Kontain VM. The validation with simply print "Hello world".

By default, the script will download and install a release mentioned in `default-release` file in this repo. A specific release can downloaded and installed by passing release name to the install script as the first argument - examples below.

#### Run script downloaded with wget

Make sure *wget is installed* and install it if needed (Fedora: `sudo dnf install wget`. Ubuntu: `sudo apt-get install wget`), and then run these commands:

```bash
sudo mkdir -p /opt/kontain ; sudo chown -R $(whoami) /opt/kontain
wget https://raw.githubusercontent.com/kontainapp/km-releases/master/kontain-install.sh -O - -q | bash
```

The above will "default" install release, i.e. the one mentioned in [./default-release](https://github.com/kontainapp/km-releases/blob/master/default-release).
To install a non-default release, e.g. `v0.1-beta2-demo`, pass the release tag to *kontain-install.sh*:

```bash
sudo mkdir -p /opt/kontain ; sudo chown -R $(whoami) /opt/kontain
wget https://raw.githubusercontent.com/kontainapp/km-releases/master/kontain-install.sh -q
chmod a+x ./kontain-install.sh; ./kontain-install.sh v0.1-beta2-demo
```

#### Run script from git repo

Alternatively, you can clone the repository and run the script directly. Note that `wget` is still needed by this script to pull the actual bundle:

```bash
sudo mkdir -p /opt/kontain ; sudo chown -R $(whoami) /opt/kontain
git clone https://github.com/kontainapp/km-releases
./km-releases/kontain-install.sh
```

Either way, the script will try to un-tar the content into /opt/kontain. If you don't have enough access, the script will advice on the next step.

### Install for Docker

You can run Kontain payload wrapped in a native Docker container.... in this case, all Docker overhead is still going to be around, but you will have VM / unikernel without extra overhead.
Install docker engine (https://docs.docker.com/engine/install/) or moby-engine (`sudo dnf install docker` on Fedora does this. `podman` is also supported.

#### Warning: cgroups version mismatch between Docker and Fedora Linux 32+

As if 10/2020 there was glitch between latest docker and latest fedora 32 configuration. While it is not related to Kontain, it  may impact `docker run` commands for fresh docker installations.

Here is a good summary of reasons, and instruction for fixes: https://fedoramagazine.org/docker-and-fedora-32/

- Symptom: **docker: Error response from daemon: OCI runtime create failed: this version of runc doesn't work on cgroups v2: unknown** error message for `docker run'.
- Fix:
  - Enable cgroups v1 on fedora32 `sudo grubby --update-kernel=ALL --args="systemd.unified_cgroup_hierarchy=0"` and reboot,
  - make sure firewall rules are not blocking docker0 interface

```bash
sudo firewall-cmd --permanent --zone=trusted --add-interface=docker0
sudo firewall-cmd --permanent --zone=FedoraWorkstation --add-masquerade
sudo systemctl restart firewalld
```

#### Kontain OCI runtime (krun)

Or you can use Kontain `krun` runtime from docker/podman or directly. `krun` is installed together with KM and other components.
`krun` is forked from Redhat's `crun` runtime github project, and can be invoked as `krun`.
One of many introductions to runtimes can be found here:
https://medium.com/@avijitsarkar123/docker-and-oci-runtimes-a9c23a5646d6

##### krun package requirements

krun needs some packages which may not be on your system, install them as follows if they are not already present on your system:

For fedora:

```bash
sudo dnf install -y yajl-devel.x86_64 libseccomp.x86_64 libcap.x86_64
```

For ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y libyajl2 libseccomp2 libcap2
```

Configuring `krun`:

##### Runtimes config for docker

Edit `/etc/docker/daemon.json` using sudo to run your editor and add the following:

```json
  {
    "default-runtime": "runc",
    "runtimes": {
      "krun": {
        "path": "/opt/kontain/bin/krun"
      },
      "crun": {
        "path": "/opt/kontain/bin/crun"
      }
    }
  }
```

Then restart docker for the change to take effect:

```bash
sudo systemctl reload-or-restart docker.service
```

Then you run a container using krun as follows:

```bash
docker pull kontainapp/runenv-python
docker run -it --runtime krun kontainapp/runenv-python -c "import os; print(os.uname())"
```

You should see output that looks like this:

```txt
posix.uname_result(sysname='kontain-runtime', nodename='ddef05d46147', release='4.1', version='preview', machine='kontain_KVM')
```

Warning: When docker is installed on Ubnuntu using `snap`, the location of config file and the name of the service are different. Please consult with snap and docker documentation for correct location and names, or simply remove the snap version and install using apt-get

### Kubernetes

In order to run Kontainerized payloads in Kontain VM on Kubernetes, each
Kubernetes node needs to have KM installed, and we also need KVM Device
plugin which allows the non-privileged pods access to `/dev/kvm`

All this is accomplished by deploying KontainD Daemon Set: KontainD serves
both as an installer for kontain and a device manager for kvm/kkm devices on
a k8s cluster. To use kontain monitor within containers in k8s, we need to
first install kontain monitor onto the node to which we want to deploy
kontainers. Then we need a device manager for `kvm` devices so k8s knows how
to schedule workloads onto the right node. For this, we developed kontaind, a
device manager for `/dev/kvm` or `/dev/kkm` device, running as a daemonset on
nodes that has these devices available.

To deploy the latest version of `kontaind`, run:

```bash
kubectl apply \
  -f https://github.com/kontainapp/km-releases/blob/master/k8s/kontaind/deployment.yaml?raw=true
```

Alternatively, you can download the yaml and make your own modifications:

``` bash
wget https://github.com/kontainapp/km-releases/blob/master/k8s/kontaind/deployment.yaml?raw=true -O kontaind.yaml
```

To verify:

```bash
kubectl get daemonsets
```

should show `kontaind` number of DESIRED equal to number of READY:

```bash
NAME               DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE   NODE SELECTOR   AGE
kontaind           1         1         1       1            1           <none>          84d
```

## Kontain technology - an outline.

Kontain provides a mechanism to create a unikernel from an unmodified application code, and execute the unikernel in a dedicated VM.
Kontain provides VM level isolation guarantees for security with very low overhead compared to regular linux processes.
For example, payloads run under Kontain are immune to Meltdown security flaw even on un-patched kernels and CPUs.

Kontain consists of two components - Virtual Machine runtime, and tools to build unikernels.
Together they provide VM-based sandboxing to run payloads.

Kontain Monitor (KM) is a host user-space process providing a single VM to a single application.
Kontain VM model dynamically adjusts to application requirements - e.g, automatically grow/shrink as application need more / less memory, or automatic vCPU add or remove as application manipulates thread pools.

Kontain does not require changes to the application source code in order to run the app as a unikernel.
Any program can be converted to unikernel and run in a dedicated VM.
If application issues fork/exec call, it is supported by spawning additional KM process to manage dedicated VMs.

- A payload is run as a unikernel in a dedicated VM - i.e. directly on virtual hardware,
  without additional software layer between the app the the virtual machine
- Kontain VM is a specifically optimized VM Model
  While allowing the payload to use full user space instruction set of the host CPU,
  it provides only the features necessary for the unikernel to execute
  - CPU lacks features normally available only with high privileges, such as MMU and virtual memory.
    VM presents linear address space and single low privilege level to the program running inside,
    plus a small landing pad for handing traps and signals.
  - The VM does not have any virtual peripherals or even virtual buses either,
    instead it uses a small number of hypercalls to the KM to interact with the outside world
  - The VM provides neither BIOS nor support for bootstrap.
    Instead is has a facility to pre-initialize memory using the binary artifact before passing execution to it.
    It is similar to embedded CPU that has pre-initialized PROM chip with the executable code

In Containers universe, Kontain provides an OCI-compatible runtime for seamless integration (Note: some functionality may be missing in this Beta release).

### Virtual Machine Prerequisites

The Linux kernel that Kontain runs on must have a Kontain supported virtual machine kernel module installed in order for Kontain to work. Currently regular Linux `KVM` module and Kontain proprietary `KKM` module are supported by Kontain.

The `KVM` module is available on most Linux kernels. Kontain requires Linux Kernel 4.15+ to properly function, 5.0+ Kernels are recommended.

Some cloud service providers, AWS in particular, do not supported nested virtualization with `KVM`. For these cases, the Kontain proprietary `KKM` module is used.

To make it easier to try Kontain on AWS, Kontain provides a pre-built AMI to experiment with KKM (Ubuntu 20 with KKM preinstalled). See below in `Amazon - pre-built AMI` section

Kontain manipulates VMs and needs access to either `/dev/kvm` device, or `/dev/kkm` device, depending on the kernel module used.

## Getting Started

For interpreted languages, Kontain provides a pre-created unikernels (packaged as Docker images). No changes to source code was made to build those.

For compiled languages (C/C++/GO/etc...) existing object files have to be linked into a unikernel. Kontain provides a convenience wrappers (kontain-gcc and kontain-g++) for C/C++. For GOLANG Kontain will handle GO executables as unikernels. For other languages, regular linux 'ld' should be used to link a unikernel and currently out of scope for this doc - though you can check kontain-gcc script for a usage example

### Using examples

`/opt/kontain/examples` directory has examples in Java, Javascript (node.js), python, C and GOLANG.

For interpreted languages (jav/javascript/python) you can either run a docker container, or extract files from there and run them directly on the host.

#### Python

Check what os.uname() reports:

```sh
docker run --runtime krun --rm  kontainapp/runenv-python -c 'import os; print(os.uname())'
```

Run a small service listening to http:

```sh
docker run --runtime krun --rm -p 8080:8080 -v /opt/kontain/examples/python/:/scripts \
   kontainapp/runenv-python  /scripts/micro_srv.py
```

And you can access it with `curl localhost:8080`. Note that the python interpreter is running as a unikernel in Kontain VM, while all user experience is vanilla Docker.

##### Running locally, without Docker

Files in `kontainapp/runenv-python` docker images (and Kontain Monitor installed) is all that's needed to run python in Kontain locally, without docker:

```sh
mkdir try-python
cd try-python
docker create --name kpython kontainapp/runenv-python
docker export kpython | tar xvf -
docker rm kpython
#
# And now you can run python in Kontain locally:
./usr/local/bin/python -c 'import os; print(os.uname())'
# get back when done
cd ..
```

Note that `./usr/local/bin/python` is really a symlink to /opt/kontain/bin/km. KM (Kontain Monitor) will find the actual unikernel to run by adding `.km` to it's own name, so the actual binary running in Kontain VM will be python unikernel from `./usr/local/bin/python.km`

#### Node.js (javascript)

Similarly, you can run a small http server from node/examples:

```sh
docker run --runtime krun -it --rm -p 8080:8080 -v /opt/kontain/examples/:/scripts \
   kontainapp/runenv-node  node /scripts/micro_srv.js
```

and then access it with `curl localhost:8080`.

Or, to run locally:

```sh
mkdir try-node
cd try-node
docker create --name knode kontainapp/runenv-node
docker export knode | tar xvf -
docker rm knode
#
# And now you can run node in Kontain locally:
./node /opt/kontain/examples/node/micro_srv.js
# get back when done
cd ..
```

#### Java

To run a simple Hello World example:

```sh
docker run --rm --runtime krun \
   -v /opt/kontain/examples/:/scripts --env CLASSPATH=/scripts/java \
   kontainapp/runenv-java-11 Hello
```

And to run locally:

```sh
mkdir try-java
cd try-java
docker create kontainapp/runenv-java-11
docker export kjava | tar xvf -
#Java needs multiple shared libs, so the command to run it locally is a bit more complex
JL=`pwd`/opt/kontain/java/lib \
   /opt/kontain/bin/km \
   --putenv=CLASSPATH=/opt/kontain/examples/java/ \
   --putenv=LD_LIBRARY_PATH=$JL/server:$JL/jli:$JL:/opt/kontain/runtime/:`pwd`/lib64 \
   ./opt/kontain/java/bin/java.kmd Hello
# get back when done
cd ..
```

#### C/C++/GO

C and GOLANG the examples are in the sections below.

### Create and run your first unikernel from a C program

Create playground directory and example program:

```C
dir=$(mktemp -d)
file=kontain-example
cat <<EOF > $dir/$file.c

#include <stdio.h>
#include <sys/utsname.h>

int main(int argc, char* argv[])
{
   struct utsname name;

   printf("Hello World from the following runtime environment: \n");
   if (uname(&name) >= 0) {
      printf("sysname \t= %s\nnodename \t= %s\nrelease \t= %s\nversion \t= %s\nmachine \t= %s\n",
             name.sysname,
             name.nodename,
             name.release,
             name.version,
             name.machine);
   }
   return 0;
}
EOF
```

Assuming you have gcc installed, compile and link the code into Kontain unikernel:

```bash
gcc -c -o $dir/$file.o $dir/$file.c
/opt/kontain/bin/kontain-gcc -o $dir/$file.km $dir/$file.o
```

Assuming access to /dev/kvm or /dev/kkm run it the unikernel:

```bash
/opt/kontain/bin/km $dir/$file.km
```

Note that `.km` is the ELF file with Kontain unikernel, and the last command was executed in Kontain VM.

To get help for KM command line, `/opt/kontain/bin/km --help`

### Build and run a Kontainer

#### Validate

To build/run the first container with Kontain Unikernel (aka `kontainer`), use regular `docker build`.
Assuming the previous example was run so the `file` and `dir` vars and the actual files are still around:

```bash
cat <<EOF | docker build -t kontain-hello $dir -f -
FROM scratch
COPY $file.km /
ENTRYPOINT [ "/opt/kontain/bin/km"]
CMD [ "/$file.km" ]
EOF

docker run --rm --runtime=krun kontain-hello
```

Regular run time can also be used for "run" commmand (but not for exec):
`docker run --rm -v /opt/kontain/bin/km:/opt/kontain/bin/km:z --device /dev/kvm kontain-hello`

NOTE: `--device /dev/kkm` on platforms with Kontain KKM module (e.g. AWS)

### Building your own Kontain unikernel - general steps

To build a Kontain unikernel, you can do one of the following

- Build or use a regular no-libc executable, e.g. GOLANG
- Link existing object files into a musl-libc based executable (like you would do for Alpine containers)
- Link existing object files into a Kontain runtime-based executable

We recommend statically linked executable (dynamic linking is supported but requires payload access to shared libraries,
so we will leave it outside of this short intro).

Kontain provides a convenience wrappers (`kontain-gcc` and `kontain-g++`) to simplify linking - these tools are just shell wrapper to provide all proper flags to gcc/g++/ld. You can use them as a replacement for gcc/g++ during a product build, or just for linking.

#### GOLANG

Unmodified statically linked GO executable can be run as a unikernel in Kontain VM, as a unikernel.
Dynamically linked executable usually use glibc - we are currently working on supporting it; an attempt to run such an executable
as a unikernel in Kontain VM will cause `Unimplemented hypercall 273 (set_robust_list)` error and payload core dump.

Here is a simple go program to prints "Hello World" and machine / sysname:

```go
#dir=$(mktemp -d)   # reuse dir from the prior example
file=kontain-example-go
cat <<EOF > $dir/$file.go
package main

import (
  "fmt"
  "syscall"
)

func charsToString(ca []int8) string {
  s := make([]byte, len(ca))
  var lens int
  for ; lens < len(ca); lens++ {
     if ca[lens] == 0 {
       break
     }
     s[lens] = uint8(ca[lens])
   }
   return string(s[0:lens])
}

func main() {
    fmt.Println("Hello world !")
    utsname := syscall.Utsname{}
    syscall.Uname(&utsname)
    fmt.Printf("Machine=%s\n", charsToString(utsname.Machine[:]))
    fmt.Printf("Sysname=%s\n", charsToString(utsname.Sysname[:]))
}
EOF

CGO_ENABLED=0 go build -o $dir/$file.km $dir/$file.go

/opt/kontain/bin/km $dir/$file.km
```

For more optimal unikernel use additional linker options:

```bash
CGO_ENABLED=0 go build -ldflags '-T 0x201000 -extldflags "-no-pie -static -Wl,--gc-sections"' -o test.km test.go
```

### Using with standard executable

Kontain supports running not-modified Linux executable as unikernel in Kontain VM, provided:

- The executable does not use GLIBC (glibc is not supported yet).
  - So the executables could be one build for Alpine, or one not using libc (e.g. GOLANG)

Build the musl-based executable with kontain-gcc, using the examples/vars above:

```bash
/opt/kontain/bin/kontain-gcc -alpine -o $dir/$file $dir/$file.o
```

And validate the result running as unikernel in Kontain VM and print out something like this:

```sh
/opt/kontain/bin/km $dir/$file
```

The result:

```sh
Hello World from the following runtime environment:
sysname         = kontain-runtime
nodename        = node-p330
release         = 4.1
version         = preview
machine         = kontain_VM
```

#### Using pre-built unikernels for interpreted languages

Python/Node/Java example above use pre-built unikernels.
You can use them in your own containers, by addint to *FROM* dockerfiles statement, or by passing scripts directories as volumes.

The images are available on dockerhub as `kontainapp/runenv-<language>-version` e.g. `kontainapp/runenv-python-3.7`. Kontain provides the following pre-built languages:

- jdk-11.0.8
- node-12.4 (js)
- python-3.7

Kontain also provides aliases: java-11, jdk-11, node and python, so you can use `kontainapp/runenv-node` without versioning .

We will extend the list by the release time. Also - see the section below on linking your own unikernel for a different version or different language if needed

Example:  You can run a interactive Python under Kontain using this: `docker run --runtime krun -it --rm kontainapp/runenv-python`

Or, without Kontain runtime:  `docker run --device /dev/kvm -it --rm -v /opt/kontain/bin/km:/opt/kontain/bin/km:z kontainapp/runenv-python-3.7`

Important: with Kontain runtime `krun`, all subprocesses (if any) and `docker exec` will be automatically wrapped in dedicated Kontain VMs (one per process). When running vanilla docker (with `--device` and `-v` flags), docker exec and and subprocesses will circumvent Kontain encapsulation.

NOTE: `--device /dev/kkm` on platforms with Kontain KKM module (e.g. AWS)
NOTE: Currently you may see debug messages there, and the container size is not optimized yet.

### Using snapshots

To speedup startup time of an application with a long warm-up time Kontain provides a mechanism to create a "snapshot" unikernel that represents the state of the application.
A snapshot is an ELF file which can be run as directly as a unikernel in Kontain VM, but captures payload state-in time.

Snapshot can be triggered by an API called from the payload, or by an external `km_cli` running on host and communicating to Kontain Monitor (KM).
See `examples/python/README.md` for details on using the API in python

Limitations:

- no coordination of multi-process payload snapshots

#### Java API

```java
package app.kontain.snapshots;

...
new Snapshot().take("test_snap", "Testing snapshot");

```

### python API

```python
    from kontain import snapshots
    snapshots.take(live=True)
```

### Using with Kubernetes

After installation of kontaind is completed (see above), you can use API server/kubectl to deploy Kontain-based pods to a Kubernetes cluster.
A spec for the pod needs to request /dev/kvm device in the `resources` section, host volume mount,
and _entrypoint calling KM (unless symlinks are set up - see below).

Example:

```yaml
  containers:
  ...
      resources:
         requests:
            devices.kubevirt.io/kvm: "1"
      command: ["/opt/kontain/bin/km", "/dweb/dweb.km", "8080"]
      volumeMounts:
        - name: kontain-monitor
          mountPath: /opt/kontain
   volumes:
      - name: kontain-monitor
        hostPath:
            path: /opt/kontain
```

## Debugging

Kontain supports debugging payloads (unikernels) via core dumps and `gdb`, as well as live debugging it `gdb`.
That also covers gdb-based GUI - e.g. Visual Studio Code `Debug`.

### Core dumps

Payload running as a unikernel in Kontain VM will generate a coredump in the same cases it would have generated if running on Linux. The file name is `kmcore` (can be changed with `--coredump=file` flag).
You can analyze the payload coredump as a regular Linux coredump, e.g. `gdb program.km kmcore`

### Live payload debugging - command line

In order to attach a standard gdb client to a km payload you need to tell the KM gdb server to listen for a client connection.
Several flags control km's activation of the internal gdb server.

```txt
 -g[port] - stop before the payload entry point and wait for the gdb client to connect, the default port is 2159
 --gdb_listen - allow the payload to run but the km gdb server waits in the background for a gdb client connection
```

You can connect to the gdb server, disconnect, and reconnect as often as you want until the payload completes.
When you connect to the km gdb server all payload threads will be paused until the gdb client starts them using the "cont" or "step" or "next" commands.

The gdb command reference can be found here: https://sourceware.org/gdb/current/onlinedocs/gdb/

Note: km gdb server testing has been done using gdb client with version "GNU gdb (GDB) Fedora 9.1-5.fc32".

KM implementation uses a dedicated signal (currently # 63) to coordinate and pause payload threads. To avoid GDB messages and stops in this internal signal , use gdb "handle SIG63 nostop" command - either for each debugging session, or add it to your ~/.gdbinit file.

If your payload uses fork() then the child payload can't be debugged with gdb.  We are working to correct this.

#### km gdb example

When starting a payload with gdb debugging enabled you would do the following and expect to see the following lines dispalyed by km.

```txt
[someone@work ~]$ /opt/kontain/bin/km -g ./tests/hello_test.km
./tests/hello_test.km: Waiting for a debugger. Connect to it like this:
        gdb -q --ex="target remote localhost:2159" ./tests/hello_test.km
GdbServerStubStarted
```

You would then run the following to attach the gdb client to the payload debugger:

```txt
[someone@work ~]$ gdb -q --ex="target remote localhost:2159" ./tests/hello_test.km
Remote debugging using localhost:2159
Reading /home/paulp/ws/ws2/km/tests/hello_test.km from remote target...
warning: File transfers from remote targets can be slow. Use "set sysroot" to access files locally instead.
Reading /home/paulp/ws/ws2/km/tests/hello_test.km from remote target...
Reading symbols from target:/home/paulp/ws/ws2/km/tests/hello_test.km...
0x0000000000201032 in _start ()
(gdb)
```

### Debugging payload child processes and exec'ed payloads

When a Kontain payload forks it inherits the debug settings from the forking process.

- If the parent waited for gdb attach before starting up, so will the child.
- If the parent was listening for a gdb client attach in the background, the child will do that also.
- Each forked payload will be listening on a new network port.
  - The new network port is the next free port that is higher than the parent's gdb network port.
  - If most ports are in use port number will wrap at 64*1024.

gdb's follow-fork-mode currently can't be used to stay with a payload's child after a fork.
There is a Kontain specific method that is used to allow a payload child process to be debugged.

Put the variable KM_GDB_CHILD_FORK_WAIT into the parent km's environment.
The value of this variable is a regular expression that is compared to the payload's name.
If there is a match, the child process will pause waiting for the gdb client to connect to km's gdb server.
The port to connect to is contained in a message from the child process's km, like this:

```txt
19:07:08.481122 km_gdb_attach_messag 319  1001.km      Waiting for a debugger. Connect to it like this:
        gdb -q --ex="target remote work:2160" /home/paulp/ws/ws2/km/tests/gdb_forker_test.km
GdbServerStubStarted
```

You can attach using this command:

```bash
gdb -q --ex="target remote localhost:2160"
```

When a payload process exec()'s, the normal gdb "catch exec" command will allow the gdb client to gain control after the exec call completes successfully.

### Visual Studio Code

VS code supports GUI debugging via GDB. `launch.json` needs to have configuration for Kontain payload debugging.... here is an example of configuratoin for debugging of `test.km` in the current directory:

```json
{
   // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
   "version": "0.2.0",
   "configurations": [
      {
         "name": "test.km",
         "type": "cppdbg",
         "request": "launch",
         "cwd": "${workspaceFolder}",
         "program": "${workspaceFolder}/test.km",
         "args": [
            "set debug remote 0"
         ],
         "stopAtEntry": true,
         "miDebuggerServerAddress": "localhost:2159",
         "miDebuggerArgs": "--silent",
         "debugServerPath": "/opt/kontain/bin/km",
         "debugServerArgs": "-g ${workspaceFolder}/test.km",
         "serverLaunchTimeout": 5000,
         "filterStderr": true,
         "filterStdout": true,
         "serverStarted": "GdbServerStubStarted",
         "logging": {
            "moduleLoad": false,
            "trace": false,
            "engineLogging": false,
            "programOutput": true,
            "exceptions": false,
            "traceResponse": false
         },
         "environment": [],
         "externalConsole": false,
         "setupCommands": [
            {
               "description": "Enable pretty-printing for gdb",
               "text": "-enable-pretty-printing",
               "ignoreFailures": true
            }
         ]
      },
   ]
```

See VS Code launch.json docs for more info

### Faktory

Faktory converts a docker container to kontain kontainer. For reference,
`container` will refer to docker container and `kontainer` with a `k` will
refer to kontain kontainer.

Faktory is an experimental proof of concept.
#### Install Faktory

faktory is pre-installed in /opt/kontain/bin/faktory

#### Java Example

In this section, we will use a container build with JDK base image to
illustrate how `faktory` works.

##### Download kontain Java runtime environment

Download the kontain JDK 11 image:

```bash
docker pull kontainapp/runenv-java-11:latest
```

##### Using faktory to convert an existing java based image

To convert an existing image `example/existing-java:latest` into a kontain
based image named `example/kontain-java:latest`:

```bash
# sudo may be required since faktory needs to look at files owned by dockerd
# and containerd, which is owned by root under `/var/lib/docker`
sudo faktory convert \
    example/existing-java:latest \
    example/kontain-java:latest \
    kontainapp/runenv-java-11:latest \
    --type java
```

### Use kontain Java in dockerfiles

To use kontain java runtime environment with dockerfile, user can substitute
the base image with kontain image.

```dockerfile
FROM kontainapp/runenv-java-11

# rest of dockerfile remain the same ...
```

Here is an example dockerfile without kontain, to build and package the
`springboot` starter `gs-rest-service` repo from `spring-guides` found
[here](https://github.com/spring-guides/gs-rest-service.git). We use
`adoptopenjdk/openjdk11:alpine` and `adoptopenjdk/openjdk11:alpine-jre` as
base image as an example, but any java base image would work.

```dockerfile
FROM adoptopenjdk/openjdk11:alpine AS builder
COPY gs-rest-service/complete /app
WORKDIR /app
RUN ./mvnw install

FROM adoptopenjdk/openjdk11:alpine-jre
WORKDIR /app
ARG APPJAR=/app/target/*.jar
COPY --from=builder ${APPJAR} app.jar
ENTRYPOINT ["java","-jar", "app.jar"]
```

To package the same container using kontain:

```dockerfile
FROM adoptopenjdk/openjdk11:alpine AS builder
COPY gs-rest-service/complete /app
WORKDIR /app
RUN ./mvnw install

FROM kontainapp/runenv-jdk-11
WORKDIR /app
ARG APPJAR=/app/target/*.jar
COPY --from=builder ${APPJAR} app.jar
ENTRYPOINT ["java","-jar", "app.jar"]
```

Note: only the `FROM` from the final docker image is changed. Here we kept
using a normal jdk docker image as the `builder` because the build
environment is not affected by kontain.

#### Run

To run a kontain based container image, use `--runtime krun` flag.
Krun will seamlessly provide access to virtualization device and neccessary pre-installed libraries:

```sh
docker run -it --rm --runtime krun example/kontain-java
```

For testing, the same command can be run without `--runtime krun` flag by passing needed info to Docker; however in this case `docker exec` and subprocesses (if any) will be executed outsided of Kontain. it should never be used this way in production.
E.g.

```bash
docker run -it --rm \
    --device /dev/kvm \
    -v /opt/kontain/bin/km:/opt/kontain/bin/km:z \
    -v /opt/kontain/runtime/libc.so:/opt/kontain/runtime/libc.so:z \
    example/kontain-java
```

NOTE: `--device /dev/kkm` on platforms with Kontain KKM module (e.g. AWS)

## Architecture

TODO - Arch. whitepaper is in editorial review

### Snapshot Overview

A KM snapshot is a file that contains a point-in-time image of a running KM guest process. When a KM process is resumed, the guest process continues from the point where the snapshot was taken.

There are two ways to create KM snapshots:

- Internal: the guest program links with the KM API and calls `snapshot.take()`.
- External: an external agent uses the `km_cli` command to create a snapshot.

A KM snapshot file is an ELF format core file with KM specific records in the NOTES section. This means that KM snapshot files can be read by standard `binutil` tools.

### Solution for no-nested-virtualization machines

When nested virtualization is not available, Kontain provides a Kontain Kernel module (`kkm`) that implements a subset of KVM ioctls. It does not reuse KVM code or algorithms, because the requiements are much simpler - but it does implement a subset if `KVM ioctls` and uses the same control paradigm. It communicates via special device `/dev/kkm`.

TODO - KKM architecture whitepaper is in editorial review

`kkm` requires to be built with the same kernel version and same kernel config as the running system. We are working on proper installation.

#### Amazon - pre-built AMI

Meanwhile, we provide an AWS image (Ubuntu 20 with pre-installed and pre-loaded KKM) which can be used to experiment / test Kontain on AWS.

Public AMI is placed in N. California (us-west-1) region. AMI name `Kontain_ubuntu_20.04` and owner is `782340374253`
To get ID using command line:

```sh
aws ec2 describe-images --owners 782340374253  --filters "Name=name,Values=Kontain_ubuntu_20.04" --query 'sort_by(Images, &CreationDate)[-1].[ImageId]'  --output text
```

As of 3/3/21 AMI image id is `ami-08631510bd83083bf` (it may change as we update the AMI), so to create a VM:

```sh
image_id=$(aws ec2 describe-images --owners 782340374253  --filters "Name=name,Values=Kontain_ubuntu_20.04" --query 'sort_by(Images, &CreationDate)[-1].[ImageId]'  --output text)
aws ec2 create-key-pair --key-name aws-kkm --region us-west-1
aws ec2 run-instances --image-id $image_id --count 1 --instance-type t2.micro --region us-west-1 --key-name aws-kkm
# before next step save the key to ~/.ssh/aws-kkm.pem and chown it to 400
```

You can then ssh to the VM , install the latest Kontain per instructions above and run it.
The only difference- please use `/dev/kkm` instead of `/dev/kvm`

### Kubernetes

#### kontaind

kontaind serves both as an installer for kontain and a device manager for
kvm/kkm devices on a k8s cluster. To use kontain monitor within containers in
k8s, we need to first install kontain monitor onto the node to which we want
to deploy kontainers. Then we need a device manager for `kvm` devices so k8s
knows how to schedule workloads onto the right node. For this, we developed
kontaind, a device manager for `/dev/kvm` or `/dev/kkm` device, running as a daemonset on nodes that has these devices available.

To deploy the latest version of `kontaind`, run:

```bash
kubectl apply \
  -f https://github.com/kontainapp/km-releases/blob/master/k8s/kontaind/deployment.yaml?raw=true
```

Alternatively, you can download the yaml and make your own modification:

``` bash
wget https://github.com/kontainapp/km-releases/blob/master/k8s/kontaind/deployment.yaml?raw=true -O kontaind.yaml
```

## Cloud

### Azure

Azure supports nested virtualization for some instances size since 2017: https://azure.microsoft.com/en-us/blog/nested-virtualization-in-azure/.
Kontain CI/CD process uses `Standard_D4s_v3` instance size.

Create one of these instances, ssh to it , then install and try Kontain as described above.

For example, assuming you have Azure CLI installed and you are logged in Azure (you may want to replace username/password and /or use ssh keys), this will create a correct VM

```bash
az group create --name myResourceGroup --location westus
az vm create --resource-group myResourceGroup --name kontain-demo --image Canonical:UbuntuServer:18.04-LTS:latest --size Standard_D4s_v3 --admin-username kontain --admin-password KontainDemo1-now?
```

Note: Kontain runs it's own CI/CD pipeline on Azure Managed Kubernetes, and AWS for no-nested-virtualization code.

### AWS

For AWS, Kontain can run on `*.metal` instances. For virtual instances, Kontain provides `kkm` kernel module and pre-build AMI with Ubuntu20.
Unfortunately AWS does not support nested virtualization  on non-metal instances, so a kernel modules is required. AMI info and example of AWS commands are documented earlier in this paper.

### Google Cloud Platform

For GCP, nested virtualization support and configuraration are described here: https://docs.google.com/document/d/1UckXwTfVgcJ5g4hvz20yJf51bDkTJs4YIwuRzFjxrXQ/edit#heading=h.z80ov6pz80hk.

Also, default Linux distro GCP creates is an older Debian with 4.9 Kernel. Kontain requires version 5.x.
Please choose either Ubuntu 18 or Ububtu 20, or Debian 10 when creating a VM

### Other clouds

Kontain works on other clouds  in a Linux VM with (1) nested virtualization enabled (2) Linux kernel 5.x

## Know Issues

Known issue are tracked in the "Known Issues" Milestone in `kontainapp/km` private repo.
Some of the key ones are duplicated here; which one make it to the next drop will depend on the feedback.


### Language systems and libraries

- CPU affinity API() are silently ignored
- Some of the huge ML packages (e.g. TensorFlow) are not tested with Python.km
- Native binaries (with glibc) are experimental and may have multiple issues - though we do test them in the CI
- Language runtime base images are only provided for a single version per language. E.g. only python 3.7. No Python 2 or Python 3.8.


### Kontain Monitor and debugging

- there is no management plane to enumerate all running Kontain VMs
- floating point status is not retained across snapshots
- snapshots are per VM - no support for coordinated snapshot for parent + childen yet
- issues in GDB:
  - Stack trace through a signal handler is not useful
  - Handle variables in thread local storage - currently ‘p var’ would generate an error
  - Floating point registers not supported
- only a small subset of /proc/self is implemented (the one we saw being used in Node.js, python 3 and jvm 11)
- getrlimit/setrlimit are not virtualized and are reditrected to the host

 ### Docker and Kubernetes

- Kontaind uses device plugin that has bugs , resulting in (rare) refusal to provide access to /dev/kvm. Workaround: re-deploy kontaid
- krun runtime is missing 'checkpoint/resume' implementation
- krun is not used on Kubernetes yet - use regular runtimes there

### Clouds

- See "platform support" earlier in this doc

## FAQ

### Is it OSS?

These are binary-only releases, Kontain code is currently not open sourced and is maintained in the a private repo. However, we are more than happy to collaborate with people who would like to hack on the code with us! Get in touch by openign an issue or emailing to info@kontain.app.

### How to install KKM

We are working on install process for KKM - it requires building with the exact kernel header version and is being worked on for the release.

### How to see Kontain interaction with KVM

- You can use `trace-cmd record` and `trace-cmd report` to observe kvm activity. [For more details on trace-cmd see](https://www.linux-kvm.org/page/Tracing).

### Are there any limitations on code running as unikernel

- The code shouldn't use not supported system calls
- VM monitor will report an error as "Unimplemented hypercall". Payload will abort
- Apps like Java or Python or Node are good with this requirement

## Contributing

These binaries and these instructions are open source because we want to interact with our community. If you're interested in working on Kontain or using it in your stack, please let us know! We would be more than happy to share the private code.

We also accept PRs and issues here. If making a large PR, please check with us first by opening an issue. Our goal is to eventually open source everything.

## Licensing

**Copyright © 2020 Kontain Inc. All rights reserved.**

**By downloading or otherwise accessing the Kontain Beta Materials, you hereby agree to all of the terms and conditions of Kontain’s Beta License available at https://raw.githubusercontent.com/kontainapp/km-releases/master/LICENSE.**

