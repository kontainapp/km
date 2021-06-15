Regular ![regular](https://github.com/kontainapp/km/actions/workflows/km-ci-workflow.yaml/badge.svg?branch=master&event=push)
Nightly ![nighty](https://github.com/kontainapp/km/actions/workflows/km-ci-workflow.yaml/badge.svg?branch=master&event=schedule)

# KM (Kontain Monitor) and related payloads for Kontain.app VMs

All terminology and content is subject to change without notice.

Kontain is the way to run container workloads "Secure, Fast and Small - choose 3". Kontain runs workloads with Virtual Machine level isolation/security, but without any of VM overhead - in act, we generate smaller artifacts and faster startup time than "classic" Docker containers.

We plan to be fully compatible with OCI/CRI to seamlessly plug into Docker/Kubernetes run time environments

For one page Kontain intro, Technical Users whitepapers and info see Kontain's [Google docs](https://docs.google.com)

:point_right: **For build and test info, see [docs/build.md](docs/build.md).**

## Files layout

* `docs` is where we keep documents and generally, keep track of things.
* `km` is the code for Kontain Monitor
* `tests` is obviously where the test are
* `make` are files supporting build system, and usually included from Makefiles
* `runtime` is the libraries/runtime for KM payloads. e.g. musl C lib
* `payloads` is where we keep specific non-test payloads, e.g. `python.km`
* `cloud` is the code for provision cloud environments (e.g. azure) to run containerized KM payloads

`README.md` files in these directories have more details - always look for README.md in a dir. If it's missing, feel free to create one and submit a PR to add it to the repo.

## Getting started

To get started, read the introduction and technical user whitepaper, then build and test and go from there.

We use Visual Studio Code as recommended IDE; install it and use `code km_repo_root/.vscode/km.code-workspace` to start, or use `File->Open Workspace` and open  **.vscode/km.code-workspace** file

## Build environment

[docs/build.md](docs/build.md) has the details, but if you want to get started right away:

* Make sure you have 'az' CLI installed

## Contributing

See CONTRIBUTING.md