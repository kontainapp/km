# Build, test, CI and sources layout

## Coding Style

We use the Visual Studio Code (VS Code) IDE.
Many of our formatting rules happen automatically when using VS Code.

TODO: Insert our style thoughts here.

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

### Dependencies

The  code is built directly on a build machine (then all dependencies need to be installed, for the list see Docker/build/Dockerfile*) or using Docker (then only `docker` and `make` are needed)

### Build system

We use GNU **make** as our build engine. This document describes some of the key targets and recipes. For list of available targets, type `make help`, or `make <tab><tab>` if you are using bash.

### Building with Docker

To build with docker, make sure `docker` and  `make` are installed, and run the commands below. *Building with docker* will use a docker container with all necessary pre-requisites installed; will use local source tree exposed to Docker via a docker volume, and will place the results in the local *build* directory - so the end result si exactly the sme as regular 'make', but it does not require pre-reqs installed on he local machine. One the the key use cases for this is cloud-based CI/CD pipeline.

Commands to build Docker image, and then build using this docker image, in Docker:

```sh
 make buildenv-image  # this will take LONG time !!!
 make withdocker
 make withdocker TARGET=test
```

Commands to build Docker image, use it to  install correct pre-requisites on the local host, and then build natively on the local host:

```sh
 make buildenv-image  # this will take LONG time !!!
 make -C tests buildenv-local-fedora # supported on fedora only !
 make ; make test
 ```

Instead of building buildenv-image, you can pull it - much faster but requires `az` command line installation and Azure login credentials. * See `build-test-make-targets-and-images.md` for details

```sh
make pull-buildenv-image
```

#### Installing Docker

Please see https://docs.docker.com/install/linux/docker-ce/fedora/ for instructions

#### Configuring Docker

By default Docker require root permission to run. Read the instructions at https://docs.docker.com/install/linux/linux-postinstall/ to run docker without being root.

### Building directly on the box

First, Install all dependencies (see `make buildenv-local-fedora` make target, and related explanation in `build-test-make-targets-and-images.md`.

Then, run make:

```sh
make
make test
```

## Build system structure

**We expect changes to the build system details so this doc is just a quick intro. For specifics and examples, see ../make/\*mk and all Makefiles.**

Build system uses tried-and-true `make` and related tricks. Generally, each dir with sources (or with subdirs) needs to have a Makefile. This Makefile need to have the following:

* The location of TOP of the repository, using  `TOP := $(shell git rev-parse --show-cdup)`
* A few variables to tell system what to build (e.g. `LIB := runtime`), and what to build it from (`SOURCES := a.c, x.c …`), compile options (`COPTS := -Os -D_GNU_SOURCE …`) and include dirs (`INCLUDES := ${TOP}/include`)
* `include ${TOP}make/actions.mk`

Generally, the build system automatically builds dependencies (unless it's configured to only scan `SUBDIRS`) and does the following work:

### Scan subdirs and execute `make` there

Indicated by setting `SUBDIRS` to a list of subdirs. They are going to be scanned and `make` invoked in each of them

### Build an executable

Indicated by setting `EXEC` to the name of executable to build. When building, the system will take in account the values in `SOURCES`, `INCLUDES` and `LLIBS` makefile vars.

### Build a library

Indicated by setting `LIB` to the target lib, e.g. `LIB := runtime`

### Build and run tests

See ../tests/Makefile. This one is work in progress and will change to be more generic and provides uniform test execution

## Containerized KM in Docker and Kubernetes (WIP)

### Build docker images for KM and python.km

Currently docker images are constructed in 2 layers:

 1. **KM + KM shared libs**. `make -C km distro` makes it, the result is `kontain/km:<tag>` image. `tag` is essentially the current branch name converted to a valid tag syntax, e.g. 'msterin-somebranch'. Just doing `make distro` in the top level will also build it as well as other distro packages.
 1. **python.km with python modules. and ALL payloads** `cd payloads/python; make distro` or just `make distro` in payloads or even at the top of the repo.
 1. Azure: Tag and push to Azure Container Registry: `make publish`. This assumes auth is all set, specifically than `az acr login -n kontainKubeACR' succeeded so docker login credentials are populated
 1. Use `make publishclean` to clean up the published stuff ()

Note that we do not tag any image as `latest` since I am not sure which one to tag :-)

### Run under Docker

`docker run --ulimit nofile=1024:1024 --device=/dev/kvm kontain/python-km:latest <payload.py>` - see a few .py files in payloads/python

### Run under Kubernetes

We do not want to run privileged containers there (and often policy blocks it anyways) and Kubernetes does not allow to simply configure access to devices , to avoid conflicts between different apps, so we use github.com/kubevirt/kubernetes-device-plugins/pkg/kvm to expose /dev/kvm to the pods.

* build the KVM plugin (we can use pre-built shared or have our own)
  * in plugins dir, do `dep ensure; make build-kvm`
* deploy to cluster as DaemonSet
  * `kubectl apply  -f <path>/kubernetes-device-plugins/manifests/kvm-ds.yaml`
* deploy KM-based apps, e.g.
  * `kubectl apply -f payloads/python/pykm-deploy.yaml (this assumes the image was pushed to dockerhub msterinkontain/kmpy - see above)

### Notes

* Currently statically linked KM  packaged to bare container using `tar c ... | docker import -`  approach - this allows to package container similar to `FROM scratch` but without having *mkdir* to create files layout.
  * this obviously does not pick up vDSO so the code there (e.g. gettimeofday) will be sub-optimal - if needed we'd mitigate it by vDSO analog for KM payloads
* Python modules are packaged into on-host Docker image layers so the payload first reaches into hypercall, we proxy it to syscall and then it goes to file names namespace and then to overlay FS. We need to see if we should just package that into the payload by mmaping directly into VM memory
* If python code reaches into  syscall we don't support yet, the container will exist with 'unknown HC', obviously. I think we need to have automatic routing to "allowed" syscalls, since it's only < 500 of those :-)
* We used manually / statically build python.tk.
* the build process uses Docker so running the build in Docker would require DinD ... I never tried it so skipping for now. **docker/kub build does not work with `make withdocker`**

## CI/CD

We use Azure Pipelines hooked into github for CI/CD. See `azure-pipeline.md` doc and `azure-pipelines.yml` configuration

## Source layout

* KM - Monitor code (executed as a user process on the host)
* runtime - libraries to be linked with payload (executed in a VM). Has subdirs with libraries used in the runtime (e.g. `./musl` and others)
* make - help / include files for the build system
* tests - tests sources

### Other repos

This repo uses a set of submodules - e.g. musl C lib and bats testing suite

=== END OF DOCUMENT ===
