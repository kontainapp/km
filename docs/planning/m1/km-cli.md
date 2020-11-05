# Release planning - use case "Kontain CLI/Toolkit"

This is a working document for "use case 2" (Provide CLI/Toolkit) from [Kontain Platform Requirments](https://docs.google.com/document/d/1LPeGZEuRdgeGx-fvsZ3Gs8ltYp6xOB7MCk10zFwtpsE/edit#)

## Status: draft

## Goal

Installable package that allows to build .km files and run them via lines, similar to `km` but with full support of virtualization (FS, network etc.) on par with Docker.

## Constraints

* We keep OCI image format and related tools.
* We focus on integration enablement and spread-the-word effort,

## Motivation

Faas (Function as a service) started with lots of noise, but lately the noise moved to "serverless" like AWS Lambdas and the likes.
We believe one of the reasons is that FaaS needs payloads that start quickly, happen millions of times per minute, and don't live long - but current isolated payloads (VMs or containers) cannot fulfill this requirements. As a result, FaaS services have to either use multi-tenant VMS for isolation, or try to minimize the VMs (e.g. Firecracker) and just pay resource overhead.

At the same time, containers are getting to be a de-facto packaging in the majority of server apps. So FaaS integrate with containers usually, but try to use long-running containers.

With Kontain, we can break this dependency, and allow FaaS to run fully VM isolated payload  after proper integration with Kontain.
Since they generally integrated with containers, we need to be as close to container management stack as possible, to lower the integration entry barrier.

Also, the work unit in FaaS is small and fleeting, so the only way to make FaaS work efficiently with container payloads is to minimize the amount of work containers management stack does in order to get a FaaS instruction stream running and completed. For us,
that likely means short circuiting as much of containers as possible yet still API and CLI compatibility, with acceptable and documented deviations

To do that we need to own (or at least being able to modify and be familiar with) management stack  - which will be fulfilled by forking and using Redhat's crun and podman stacks.


## Requirements

Currently `km` CLI nicely starts KM with payload, but Virtualization is not completed - e.g. we are missing network, there are known gaps in pid and FS (e.g. /proc) files, there is no support for mount or explicit pivot_root, etc.  P0 is to solve that. Here is a longer list:

* P0: Namespaces/RootFS/networking is not natively virtualized
* P0: Anything in the payload image can be run **only** in Kontain VM when started via this CLI.
  * P1: only payload specific files should be in the image. KM itself is installed on the host. This is needed to assure KM itself cannot be messed with (e.g. replaced with a malicious script  :-)) in a customer image
* P1: Config is somewhat limited to command line, would make it much harder for integrations
* P1: Does not comply with any standard so "yet another thing to learn" for devs doing integration

## Approach

* Payload images have the PAYLOAD only (see `runenv-image` images in your build). All tools (inlcuidng KM ) needed to run those will be installed on the host
* We will modify crun (RedHat OCI implementation) to support KM payloads with either additional flag or (better) additional property in the spec file
  * crun is selected due to (1) takes only half of crun time/resources (2) written in C so easier to coexist with KM (well, Go coexists just fine but C version is faster and better mapped to our skill set)
  * Modified version is named 'krun'. It should be a branch in forked github public repo, under GPL
* validate this work with dockerd `--add-runtime` flag (or daemon.json config) and related docker `--runtime` flag (see https://docs.docker.com/engine/reference/commandline/dockerd/)
* We will use Redhat's Podman as management CLI (with --runtime=kontain)
  * Podman is MUCH faster (0.1sec overhead vs Docker 0.5 sec on 'hello-world'), with seccomp and veth creation taking 1/3 of this time, each
  * Podman does not require a service and can be much easier profiled to understand and improve per characteristics
  * Unfortunately it won't help with Kubernetes start time - we delay it for now, but the place to look into start time there will be CRI-O

`crun` is a CLI wrapper for a library. We can use library directly or use the CLI - the trade off will be extra fork for more work. At this point, we choose extra fork  - we'll use crun executable as a base and fork KM as needed
  * If we use crun library from KM, we can extend the runtime spec with KM-specific info and read the spec from KM
  * [this is Plan of Record] If we use 'krun' CLI derived from crun directly, we can still extend the spec and use the info to form proper command line flags.


### Notes

* we have a big chunk of PoC code in 'runk' which was done by Eric in GO, using early KM builds.
  * `This code (and Eric) should be consulted when needed`, but the C implementation is twice as fast as our Go implementation (and redhat is planning to make it default) so we will stick with it
  * Some of the features in KM, e.g. --wait-for-signal, were done specifically for this. These features, and best integration approach, will to be revisited (e.g. --wait-for-signal dropped in favor of non-racing solution, e.g. named pipe - see below)
    * From Eric: "Just to clarify, this feature was not used anymore. OCI spec states that runk create should set up everything up until the payload fork and exec, then wait. Runk start should call fork and exec to launch the payload process. However, when calling runk create and runk start back to back, there is a race condition where runc create calls km --wait-signal but runc create returned before km actually waits on the signal. So far, I can only think a named pipe would avoid this race condition where waiter and caller needs to both enter to be valid."

## Burst creation behavior (P0 for info, P2 for fixes)

We know that docker is choking when containers are created in burst; some pretty low number (don't recall exactly but I think double digit is enough) will do the job. We need to validate burst behavior of podman. If it's not good, we'd need to understand why and hopefully correct and submit PR. It is a P0 to understand the external behavior, and P2 to find the culprint (if any) and repair

## Mixed payloads and 'crun exec' (P2)

*** FOR DISCUSSION ****  This a P2 for the first release, but it keeps popping up as a question in different discussions so keeping it here fo reference and for capturing points and opinions

In many cases, containers entry point is customer's bash script, which set environment and invokes the actual payload.
Also, for troubleshooting customers often do "exec bash" into container.
We want to be able to enforce "all payload runs in Kontain VM" without completely breaking the above.

Ideally we should support basic shells in KM for troubleshooting needs. As a scaffold until shells are supported, we can have an explicit array in config.json that enumerates non-KM executables (e.g busbox 'ash' statically linked, or the likes) which can be invoked via 'exec' (and entrypoint/command) without KM. E.g. we'd have python (symlink to KM ) and python.km (actual payload) in our base image. The customer can add 'ash' and all folders with the actual app. Then they can pass a flag to modified podman '--allow-direct-invokation=/bin/ash' and it's passed to krun, so when 'krun exec /bin/ash' is called, /bin/ash starts directly, without KM. (TBD: check how podman works with spec extensions).

An alternative here is to have ash/sh and maybe even BASH working under .km

## in-KM virtualization

Crun (and other OCI runtimes) do all fs/pid/uuid/network virtualization we need. We can just bind mount KM inside of the container and have it run under KM without any modifications. However, that removes control over what's going on and locks us to performance characteristics of podman (or docker) management stack

While it's not the goal to have startup time dropped to minimum, we still want to be minimize if we see customers' interest - e.g. if integration attempts with FaaS are made. So the approach we take is using our own github fork of crun as "kontain" runtime, ask customer to use `--runtime=kontain` and adjust internals when needed - P0 for KM bind mount inside of the container.

Here is what we can certainly accelerate using this approach (but won't in the first release):

* seccomp configure (including BPF spec parse) is said to take about 1/3 of container initialization time. We need to validate it with podman/crun, and have in the backpocket a design (or even implementation) of configuring hcall system to do the same without the need for seccomp.... in case  this time will make any difference for customers

 * misc, namesspaces and capabilities can be turned off in the spec, which allows us to check for time consumed by each of these
* specifics on networking are only known on runtime, so the runtime is getting (from a caller, e.g. containerd or podman) a command line to create/configure veth

## Pre-packaged languages (P0)

**PRIORITY TO BE DISCUSSED**

We will pre-package Java, Python and Node in "runenv" type images, place them on public dockerhub/kontainapp, and provide doc on how to run stuff by either using `FROM` in customer's dockerfile, or just passing volumes with script (the latter will certainly be documents in "getting started" doc).

"Getting started" doc will have example to run basic HTTP servers using the stuff above.

It will also have example of running GO http server directly.

## Install experience (P0)

The goal here is to

1. Validate pre-requisites (kernel version; vmx/smx extension presence, kvm enabled, presence of packages e.g. podman)
1. Install all needed components for KM run on it's own
1. Install all needed components for building .km files
1. Install  all needed components run KM with payloads as OCI runtime

The experience will be as easy as

```bash
wget -O - kontain_install_script_url | sudo bash -x
```

It will install /opt/kontain with KM, libs and tools (`we may need to place things like gcc-kontain in /opt/kontain/bin`), necessary pieces for krun and anything else needed. See `cloud/k8s/kontaind/installer.sh` for a partial implementation in K8S

After all is installed, will will print out a location with  online links for getting started. All docs will be online in public `github/kontain/kontain` (name tentative)

Also, usage() in km (and any other CLI we install) will provide the same link. As an alternative we can place in in `man 7 kontain`

## Documentation

The following docs will have to be written

* install and getting started
  * simple C / Go  build and run
  * using of pre-created python/Node/Java with `-v http_server_script`
* snapshots - getting started
* using Dockerfiles and FROM - examples
  * Kontainers and snapshot - how to use
* using with Kubernetes (existing Eric's work, no custom runtime)
* krun runtime - how to use (really just extensions)
* KM Architectures, advantages
  * direct usage of KM monitor, including symlinks, forks
  * logging
* timing comparison on simple things (start, )
* contact info ( I guess just issues on github)

