# Release planning - use case "Kontain CLI/Toolkit"

This is a working document for "use case 2" (Provide CLI/Toolkit) from [Kontain Platform Requirments](https://docs.google.com/document/d/1LPeGZEuRdgeGx-fvsZ3Gs8ltYp6xOB7MCk10zFwtpsE/edit#)

## Status: draft

## Goal

Installable package that allows to build .km files and run them via lines, similar to `km` but with full support of virtualization (FS, network etc.) on par with Docker.

## Constraints

* We keep OCI image format and related tools.
* We focus on integration enablement and spread-the-word effort,

## Requirements

Currently `km` CLI nicely starts KM with payload, but Virtualization is not completed - e.g. we are missing network, there are known gaps in pid and FS (e.g. /proc) files, there is no support for mount or explicit pivot_root, etc.  P0 is to solve that. Here is a longer list:

* P0: Namespaces/RootFS/networking is not natively virtualized
* P0: Anything in the payload image can be run **only** in Kontain VM when started via this CLI.
  * P1: only payload specific files should be in the image. KM itself is installed on the host. This is needed to assure KM itself cannot be messed with (e.g. replaced with a malicious script  :-)) in a customer image
* P1: Config is somewhat limited to command line, would make it much harder for integrations
* P1: Does not comply with any standard so "yet another thing to learn" for devs doing integration

## Approach

* Modify crun (RedHat OCI implementation) to support KM payloads with either additional flag or (better) additional property in the spec file
  * crun is selected due to (1) takes only half of crun time/resources (2) written in C so easier to coexist with KM (well, Go coexists just fine but C version is faster and better mapped to our skill set)
  * Modified version is named 'krun'. It should be a branch in forked github public repo, under GPL
* validate this work with dockerd `--add-runtime` flag (or daemon.json config) and related docker `--runtime` flag (see https://docs.docker.com/engine/reference/commandline/dockerd/)

### Notes

* we have a big chunk of PoC code in 'runk' which was done by Eric in GO, using early KM builds.
  * This code should be consulted when needed, but the performance is 2x as fast with C implementation (and redhat is planning to make it default) so we will stick with it
  * Some of the features in KM, e.g. --wait-for-signal , were done specifically for this. These features, and best integration approach, need to be revisited
    * From Eric: "Just to clarify, this feature was not used anymore. OCI spec states that runk create should set up everything up until the payload fork and exec, then wait. Runk start should call fork and exec to launch the payload process. However, when calling runk create and runk start back to back, there is a race condition where runc create calls km --wait-signal but runc create returned before km actually waits on the signal. So far, I can only think a named pipe would avoid this race condition where waiter and caller needs to both enter to be valid."
* `crun` is a library and a CLI wrapper. We will have to look at the both and decide if we use library directly or use the CLI.
  * If we use crun library from KM), we can extend the runtime spec with KM-specific info and read the spe
  * if we decide to use 'krun' CLI derived from crun directly, we can still extend the spec and use the info to form proper command line flags.

## Work items and costs

Costs estimation is TBD

* Packaging
  * Package km in tar.gz or image and provide installation script (to avoid rpm/etc... for now)
  * adding tools (e.g. gcc-kontain)
  * documentation for building KMs with gcc-kontain, examples of links
    * we use OCI images, so the image work / examples have to be documented and tested but no code for image manipulation
  * optionally pre-build python.km and maybe java.km ? (to be decided)
* prep and push GPL repo, set CI on it (maybe just another submodule?)
* analyze and decide on library vs crun CLI usage
* add extra property to spec to indicate Kontain (can we automate it?), and extra dictionary for Kontain params
  * maybe as a flag to 'krun spec'
* add km invocation to 'krun run' , 'krun exec'
* Analyze and integrate checkpoints... (not sure if snapshots are related)
* Update and integrate pause/resume and kill
* Test:
  * Validate implementation: https://github.com/opencontainers/runtime-tools
  * Pef validation
  * testing with Docker (per doc above)
  * P1: testing with CRI-O by setting additional runtime

## Open Items

Open items may be resolved after an explicit investigation and discussion, or after some time passing and us getting enough data

* Using the lib vs using the CLI (depends on the need to to fork or clone for namespaces so the investigation for this goes first)
* What do we do about "bundle" (rootfs+spec) creation ? If we are OCI runtime compatible, management (dockerd/containerd/etc..) will take care of this. If we are not, we'd need to have an answer
* Do we bundle python.km and other payloads ? if yes - design. (I suggest making runenv-image and have scripts pulling and unpackaging)