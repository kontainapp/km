# M1 planning

This directory contains dev planning docs (architecture, design) for M1 planning. Work items and open issues are tracked in this file (README.md)

M1 is the release we want to put together by Oct'20.

Requirements and experience are documented in [Kontain Platform Requirements](https://docs.google.com/document/d/1LPeGZEuRdgeGx-fvsZ3Gs8ltYp6xOB7MCk10zFwtpsE/edit#) . Note that we keep  code-related stuff in the code but make requirements/use cases/experience docs in google docs to make it easier to review by folks not on git

## Work items and costs (draft)

- [ ] Snapshots
  - [ ] Java API for snapshots (code/test/docs)
  - [ ] Java Snapshot / acceleration doc on km-releases
- [ ] runenv-image
  - [ ] add versioning scheme (runenv-python-3.7:SHA)
- [ ] krun/runtimes
  - [ ] add submodule, add to CI
  - [ ] support `exec` via KM
  - [ ] ? review/support other actions (`checkpoint/resume` mainly)
  - [ ] do test runs with docker of podman using our runtime (not in M1?)
  - [ ] use our runtime in additional Kube test (not in M1)
- [ ] Release process
  - [ ] Release version, tags and relations to km repo SHA - design
  - [ ] Github workflow to build a release
  - [ ] Examples separation from Doc , and code to test them
  - [ ] AMI creation automation (not in M1) ,  and AMI usage docs/test (in M1)
  - [ ]
- [ ] KKM OSS - I think we need to publish the code
  - [ ] Business decision (Mark Davis)
  - [ ] Versioning / dependencies between KKM and KM (discuss/implement)
  - [ ] Implementation - either a wrapper, or publishing KKM repo with proper license and doc.
- [ ] LICENSING for the release (Mark Davis)
- [ ] Documentation - see km-releases/README.md. `some of the sections exit but need to be reviewed by editor AND the team, and we need owners`
  - [ ] user guide for individual tools - KM, kontain-gcc, building workloads using ld directly, memory layout
  - [ ] user guide for images and runenv and mounts
  - [ ] user guide for KKM (architecture)
  - [ ] working with AWS (based on kkm AMI)
  - [ ] architecture write up (with pictures) for KM
  - [ ] debugging (gdb, tracing, vscode debug)
  - [ ] Kubernetes and kontaind (inclding details on /dev/kvm /dev/kkm plugin), examples
  - [X] Faktory (the doc is a bit laconic)
  - [ ] runenv - how to build one, how to use existing, examples
  - [ ] languages support (versions, what else we support/don't)


### Demo

- The goal is to (1) present to investors (2) publish for release.
- Basic install I: create Azure VM. while it is installing , do faktory demo
- Factory demo: https://github.com/kontainapp/km/tree/master/demo/faktory/java
- Basic install II: Install gcc, fix /dev/kvm. Follow https://github.com/kontainapp/km-releases/blob/master/GettingStarted.md
- Build + run first unikernel

- [ ] TODO: fix snapshot (John is working on it), when it is fixed - demo it by following https://github.com/kontainapp/km/tree/master/demo/spring-boot

### Next Milestone - open

- [ ] .deb. .rpm and apkbuild support for packaging
- [ ] build / install for KKM (per-version per-distro; and/or compile on install .... can use podman)
- [ ] Faktory - wider tests with java and python
- [ ] SaaS Faktory (low scale)
  - [ ] UI design / wireframes / backend
  - [ ] outsource code (?)
- [ ] Security monitoring and control (investigate)
  - [ ] Seccomp vs hcall - define filters
- [ ] Snapshot management (km_cli to relate snapshots to original, show relations, etc)
- [ ] "this payload is not going to run" analysis tool (phase 1 -  what (if anything) can be done for known non-supported APIs ?)
- [ ] UI plugin for Visual Studio (Similar to Docker)
  - [ ] list of Kontainer images and `running Kontain VMs`; stop/kill/snapshot operations, info on files etc.
  - [ ] Faktory
  - [ ] debugging

### Release Process

- Update and integrate pause/resume and kill
- Test:
  - Validate implementation: https://github.com/opencontainers/runtime-tools
  - Pef validation
  - testing with Docker (per doc above)
  - P1: testing with CRI-O by setting additional runtime

## Open Items

- snapshot experience (it's is worked on in java-start.md and will have to be documented in the `documentation`)
- more TBD - please expand as you see fit
