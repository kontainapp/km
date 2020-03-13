# Build, test, CI and sources layout

## Coding Style

We use the Visual Studio Code (VS Code) IDE. Our workspace (`km_repo_root/.vscode/km.code-workspace`) is configured to automatically run clang-format on file save.

**If you are not useing VS Code, it is your responsibility to run clang-format with `.clang-format` (from the repo) before any PR**

Some of the style points we maintainin C which are not enforced by clang:

* never use single line `if () statement;` - always use `{}` ,i.e. `if() {statement;}`
* single line comments are usually `//` , multiple lines `/* ... */`
* more to be documented

In Python we follow [Pep 8](https://www.python.org/dev/peps/pep-0008/)

## Build and test

### Cloning the repo

We use git submodules, so do not forget to init/update them. Assuming your credentials are in order, here is how to bring the stuff in:

```sh
git clone git@github.com:kontainapp/km.git
cd km
git submodule update --init
```

Going forward, do not forget to add --recurse-submodules to `git pull` and `git fetch`, or simply configure it to happen automatically:

```sh
git config --global fetch.recurseSubmodules true
```

### Build system

We use GNU **make** as our build engine. This document describes some of the key targets and recipes. For list of available targets, type `make help`, or `make <tab><tab>` if you are using bash.

### Dependencies

If you build with Docker (see below) then only `docker` and `make` are needed for the build. If you build directly on your machine, dependencies need to be installed and build environment prepared - steps are below in **build buildenv** and **install dependencies** sections.

`A recommended way is to download Docker image with build environment from Azure`, and then either use it for building with docker, or use it for installing dependencies locally and building directly on the box. Going this way will also test your access to Azure, which you will need to see results of CI runs.

#### Login to Azure - interactive

There are 2 way to login - interactive (`make -C cloud/azure login` and non-interactive (`make -C cloud/azure login-cli`).

When you run interactive login, a browser will be open and you need to enter your credentials (@kontain.app) for authentication with Microsoft live.com service. If you are presented with "Work or home" choice, choose Work.

#### Login to Azure - non interactive

For non-interactive login to work, you need to create and store personal credentials on your machine.  Here are the steps:

1. Login to Azure interactively
1. Create personal Security Principal for non-interactive logins: `az ad sp create-for-rbac -n "https://your_alias_noninteractive" --role contributor`, e.g. `az ad sp create-for-rbac -n "https://msterin_noninteractive" --role contributor`
1. Save the results in local file, e.g. *~/.tokens/az_msterin_noninteractive*. Set chmod 400 on the file. This is secret data, so **DO NOT PUBLISH AND DO NOT SAVE TO GIT**.
1. Add credential to .bash_profile - code below (modify the file var first). If you use other shells, modify the script accordingly.
1. `source ~/.bash_profile`
1. **Test the login:** `make -C cloud/azure login-cli`

Code to convert `az ad sp create-for-rbac` results to env variables needed by Makefiles:

```sh
file=~/.tokens/az_msterin_noninteractive
cat $file  | jq -r  '"export SP_APPID=\(.appId)",
                     "export SP_DISPLAYNAME=\(.displayName)",
                     "export SP_NAME=\(.name)",
                     "export SP_PASSWORD=\(.password)",
                     "export SP_TENANT=\(.tenant)"' \
               >> ~/.bash_profile

```

### Pull the 'buildenv' image from Azure Container Registry

```sh
make -C tests pull-buildenv-image
```

#### build the 'buildenv' image locally using Docker

An alternative way is to build 'build environment' images locally:

```sh
make -C tests buildenv-image  # this will take VERY LONG time as it builds gcc and C++ libs
```

#### Install dependencies on the local machine (after buildenv image is available)

```sh
make -C tests buildenv-local-fedora  # currently supported on fedora only !
```

### Building with Docker

#### Installing Docker

Please see https://docs.docker.com/install/linux/docker-ce/fedora/ for instructions

#### Configuring Docker

By default Docker require root permission to run. Read the instructions at https://docs.docker.com/install/linux/linux-postinstall/ to run docker without being root.

#### Building

To build with docker, make sure `docker` and  `make` are installed, and buildenv image is available (see section about this above) and run the commands below.

*Building with docker* will use a docker container with all necessary pre-requisites installed; will use local source tree exposed to Docker via a docker volume, and will place the results in the local *build* directory - so the end result is exactly the same as regular 'make', but it does not require pre-reqs installed on the local machine. One the the key use cases for build with docker is cloud-based CI/CD pipeline.

```sh
 make withdocker
 make withdocker TARGET=test
```

### Building directly on the box

First, Install all dependencies using `make -C tests buildenv-local-fedora` make target (specific steps are above, and detailed  explanation of images is `in docs/image-targets.md`

Then, run make from the top of the repository:

```sh
make
make test
```

## Build system structure

At any dir, use `make help` to get help on targets. Or , if you use bash, type `make<tab>` to see the target list.

Build system uses tried-and-true `make` and related tricks. Generally, each dir with sources (or with subdirs) needs to have a Makefile. Included makfiles are in `make/*mk`.

This Makefile need to have the following:

* The location of TOP of the repository, using  `TOP := $(shell git rev-parse --show-toplevel)`
* A few variables to tell system what to build (e.g. `LIB := runtime`), and what to build it from (`SOURCES := a.c, x.c …`), compile options (`COPTS := -Os -D_GNU_SOURCE …`) and include dirs (`INCLUDES := ${TOP}/include`)
* `include ${TOP}/make/actions.mk` or include for `images.mk`
* Config info is in `make/locations.mk`**

**Customization you may want to do to compile flags and the likes for you private builds are in `make/custom.mk`**

Generally, the build system automatically builds dependencies (unless it's configured to only scan `SUBDIRS`) and does the following work:

### Scan subdirs and execute `make` there

Indicated by setting `SUBDIRS` to a list of subdirs. They are going to be scanned and `make` invoked in each of them

### Build an executable

Indicated by setting `EXEC` to the name of executable to build. When building, the system will take in account the values in `SOURCES`, `INCLUDES` and `LLIBS` makefile vars.

### Build a library

Indicated by setting `LIB` to the target lib, e.g. `LIB := runtime`

### Build and run tests

See ../tests/Makefile. This one is work in progress and will change to be more generic and provides uniform test execution

## Containerized KM in Docker and Kubernetes

### Build docker images

We have 3 type of images - "buildenv" allowing to build in a container, "testenv" allowing to test in a container (with all test and test tools included) and "runenv" allowing to run in a container. See `docs/image-targets.md` for details.

### Run under Docker

The runenv image will only contain KM payloads. KM is designed to be
installed on the host and mounted into the container when running. Here is
how to build and validated runenv image for KM payloads:

```sh
make -C payloads runenv-image 
make -C payloads validate-runenv-image
```

This builds `kontain/runenv-<payloads>` and run a test.

### Run under Kubernetes

#### Kubernetes config

* make sure `kubectl` is installed. On Fedora, `sudo dnf install kubernetes-client`
* if you use our Azure Kubernetes cluster, make sure you are logged in (`make -C cloud/azure login`).
* if you (for some reason) want to use your own cluster, read the section below

##### [only needed for new clusters] Prep cluster to allow /dev/kvm access

We do not want to run privileged containers there (and often policy blocks it
anyways) and Kubernetes does not allow to configure access to devices
, to avoid conflicts between different apps. KM also needs to be installed
onto each node. We combined these functionalities into `kontaind`. `kontaind` uses
github.com/kubevirt/kubernetes-device-plugins/pkg/kvm to expose /dev/kvm to
the pods, and a installer of our own implementation.

* deploy `kontaind`
```sh
make -C cloud/k8s/kontaind runenv-image
make -C cloud/k8s/kontaind push-runenv-image
make -C cloud/k8s/kontaind install
```

#### Push and Run demo apps

From the top of the repo, build and push demo containers:

```sh
make runenv-image
make push-runenv-image
```

* To deploy KM-based apps:  `kubectl apply -k ./payloads/k8s/azure/python/`
* To clean up: `kubectl delete -k ./payloads/k8s/azure/python/`

:point_right: **Note** we use `kustomize` support in kubectl, it's discussion is beyond the scope of this doc, especially for demo. See `payloads/demo_script.md` for more info on the demo

## CI/CD

We use Azure Pipelines hooked into github for CI/CD. See `azure-pipelines.yml` for  configuration and `azure-pipeline.md` doc for info, including FAQ (e.g how to run CI containers manually)

### Other repos

This repo uses a set of submodules - e.g. musl C lib and bats testing suite

=== END OF DOCUMENT ===
