Regular ![regular](https://github.com/kontainapp/km/actions/workflows/km-ci-workflow.yaml/badge.svg?branch=master&event=push)
Nightly ![nightly](https://github.com/kontainapp/km/actions/workflows/km-ci-workflow.yaml/badge.svg?branch=master&event=schedule)

# KM (Kontain Monitor) and related payloads for Kontain.app VMs

All terminology and content is subject to change without notice.

Kontain is the way to run container workloads "Secure, Fast and Small - choose 3." Kontain runs workloads with Virtual Machine level isolation/security, but without traditional VM overhead; in fact, we generate smaller artifacts and faster startup time than "classic" Docker containers.

We plan to be fully compatible with OCI/CRI to seamlessly plug into Docker/Kubernetes runtime environments

For general information, including a one-page Kontain intro and whitepapers for technical users, see [Kontain's Google docs](https://drive.google.com/drive/folders/1QiqdN7GrHldNSyvwzOiQYXT-PM1WHS29?usp=sharing)

:point_right: **For build and test info, see [docs/build.md](docs/build.md).**

## Files Layout

* `docs` is where we keep documents and generally keep track of things
* `km` is the code for Kontain Monitor
* `tests` is (obviously) where the test are
* `make` includes files supporting the build system, and usually included from Makefiles
* `runtime` is where to find the libraries/runtime for KM payloads, e.g. musl C lib
* `payloads` is where we keep specific non-test payloads, e.g. `python.km`
* `cloud` is the code for provisioning cloud environments (e.g. Azure) to run containerized KM payloads

`README.md` files in these directories have more details - always look for README.md in a dir. If it's missing, feel free to create one and submit a PR to add it to the repo.

## Getting Started

To get started, read the Introduction and Technical User Whitepaper, then build and test and go from there.

We use Visual Studio Code as the recommended IDE; install it and use `code km_repo_root/.vscode/km.code-workspace` to start, or use `File->Open Workspace` and open  **.vscode/km.code-workspace** file

## Build Environment

[docs/build.md](docs/build.md) has the details, but if you want to get started right away:

* Be sure you have 'az' CLI installed

## User Documentation

[Kontain User Guide](docs/user-guide.md) - Provides information for developers to install Kontain and use it to run workloads. 
 
[Debugging Kontain Unikernels](docs/debugging-guide.md) - Provides information for developers about how to debug a Kontain workload (unikernel) using standard debugging tools and practices.  

For command-line help: `/opt/kontain/bin/km --help`

[Known Issues](https://github.com/kontainapp/km/blob/master/docs/known-isssues.md) 

[Kontain FAQs](docs/FAQ.md) 

## How to Find Support or File an Issue

Contact <community@kontain.app> 

## Contributing
See [CONTRIBUTING.md](https://github.com/kontainapp/km/blob/master/CONTRIBUTING.md)

## Licensing
Copyright © 2021 Kontain Inc. All rights reserved.

By downloading or otherwise accessing Kontain's Beta materials, you hereby agree to all of the terms and conditions of [Kontain's Beta License](https://github.com/kontainapp/km/blob/master/LICENSE).
