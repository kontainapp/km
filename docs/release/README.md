# Kontain Release

Version: 0.9-Beta
Document Status: placeholder

## Introduction

Kontain provides a way to run unmodified or relinked Linux executables as unikernels in a dedicated VM  - with VM level isolation guarantees for security, and delivers very low overhead compare to regular linux processes. For example, payloads run under Kontain are immune to Meltdown security flaw even on un-patched kernels and CPUs.

While providing VM-level isolation, Kontain requires container-level (or less) overhead. In many cases Kontain can accelerate start time and smoother burst costs.

Kontain VM model dynamically adjusts to application requirement - e.g, automatic grow/shrink as application need more / less memory, or automatic vCPU add or remove as application manipulates thread pools.

Kontain consists of library based unikernel and user-space Kontain Monitor providing application with VM model. Together they provide VM-based sandboxing to run payloads.

* Kontain Monitor (KM) is a host user-space process providing a single VM to a single application.
  * If application issues fork/exec call, it is supported by spawning addintional KM process to manage dedicated VMs.
* A payload is run as a unikernel - i.e. directly on virtual hardware, whithout  additional software layer between the app the the virtual machine. The payload  may be the original executable, untouched (see below for more details) or original object files re-linked with Kontain libraries.
  * Kontain does not require changes to the application source code in order to run the app as a unikernel.
* Kontain VM is a specifically optimized VM Model. While allowing the executable to use full user space instruction set of the host CPU, it provides only the features necessary for the unikernel to execute.
  * CPU lacks features normally available only with high privileges, such as MMU and virtual memory. VM presents linear address space and single low privilege level to the program running inside, plus a small high privilege landing pad for handing traps and signals.
  * The VM does not have any virtual peripherals or even virtual buses either, instead it uses a small number of hypercalls to the KM to interact with the outside world.
  * The provides neither BIOS nor support for bootstrap. Instead is has a facility to pre-initialize memory using the binary artifact before passing execution to it. It is similar to embedded CPU that has pre-initialized PROM chip with the executable code.

In Containers universe, Kontain provides an OCI-compatible runtime for seamless integration (Note: some functionality may be missing on this Beta release).

## Install

Kontain releases are maintained on a public git repo https://github.com/kontainapp/km-releases, with README.md giving basic download and install instruction.

* Generally , it's just `wget -q -O - https://raw.githubusercontent.com/kontainapp/km-releases/master/kontain-install.sh | bash` - but please see the above link for the latest.

### Pre-requisites

Kontain manipulates VMs and needs either `KVM module and /dev/kvm device`, or Kontain proprietary `KKM module and related /dev/kkm device`.

At this moment, either KVM should be available (locally on or Azure/GCP instance), or you can use AWS Kontain-Ubuntu AMI to experiment with KKM (Ubuntu 20 with KKM preinstalled)

===> KKM install requires building with the proper kernel header version snd is being worked on for the release. For
===> Currently the install untars the necessary files and checks for pre-requisites. We plan to provide native packaging (.rpm/.deb/apkbuild)

### Docker

You can run Kontain payload wrapped in a native Docker container.... in this case, all Docker overhead is still going to be around. Or you can use Kontain `krun` runtime from docker/podman or directly. `krun` is installed together with KM and other components.

Configuring `krun`: Documentation here is TBD

* runtimes config
* validate:
  * running in docker with manual mount
  * running in podman with --runtime

### Kubernetes

In order to run Kontainerized payloads in Kontain VM on Kubernetes, each Kubernetes node needs to have KM installed, and we also need KVM Device plugin which allows the non-priviledged pods access to /dev/kvm

All this is accomplished by deploying KontainD Daemon Set:

Documentation TBD

* Install kontaind
* validate a simple run

## Getting started

### Building your own Kontain unikernel

To build a Kontain unikernel, you can do one of the following

* Build or use a regular no-libc executable, e.g. GOLANG
* Link existing object files into a musl-libc based executable (like you would do for Alpine containers)
* Link existing object files into a Kontain runtime-based executable

We recommend statically linked executables (dynamic linking is supported but requires payload access to shared libraries , so we will leave it outside of this short into).

Kontain provides a convenience wrappers (`kontain-gcc` and `kontain-g++`) to simplify linking - though these tools are just shell wrapper to provide all  proper flags.


#### GOLANG


### Using with existing executables

Kontain currently support running not-modified Linux executables as unikernels in Kontain VM, provided:

* The executable does not use GLIBC (glibc is not supported yet).
  * So the executables could be one build for Alpine, or one not using libc (e.g. GOLANG)
* The executable is not using some of the exotic/not supported syscalls (more details on this later, but apps like Java or Python or Node are good with this requirmenent)

* Example: alpine Hello world


### Using pre-build unikernels for interpreted languages

General approach - we will just use runenev-\<payload\> here for the payloads:

* python
* java
* node.js

### Using snapshots

 * Manual example (boot?)

### Bring your own dockerfiles

* example with FROM runenv

### Using with Kubernetes

* dweb ?

### Debugging

## Faktory

## Architecture

### Kontain Monitor (VM hardware model) and Kontain unikernels

Link to google doc if we have something.
System design diagram
Hardware model - threads, memory, IO
Virtualization levels (in-KM, outside of KM)
Pillars (no memory mgmt, delegation to host, etc)
KM code components

Supported syscalls - philosophy
Sandboxing vi OUT/hcalls
Sandboxing via syscall intercept
Supported syscals and delegation to host
  relations to seccomp

### Solution for no-nested-kvm

KKM architecture (high level)

OPEN: how do we let people build it for their kernel if needed ?

### Kontainers

Regular runtime - mount, namespaces, using snapshots. Pluses and minuses
krun - why and how to use
*  OCI spec and use runtimes for bringing in existing kms. Update doc on how to use then and what needs to be in. Add an example
```
  {
  "default-runtime": "runc",
  "runtimes": {
    "crun": {
      "path": "/usr/local/bin/crun",
      "runtimeArgs": [
              "--kontain"
      ]
    }
  }
  ```

### Kubernetes

* Kontaind and kvm plugin

## dev guide

* Tools to build simple unikernels with kontain-gcc
* Example of building complex unikernels (e.g python)
* Running alpine exec as-is in a unikernel

## Cloud

### Azure with nested KVM

#### Azure Kubernetes

### AWS (no-nested KVM)

### Other clouds

summary of how to do go there

======= END OF THE DOC ====
