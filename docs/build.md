# Build, test, CI and sources layout

## Build and test

### Cloning the repo

We use git submodules, so do not forget to init/update them. Assuming your credentials are in order, here is how to bring the stuff in:

```sh
git clone git@bitbucket.org:kontainapp/covm.git
cd covm
git submodule update --init
```

Going forward, do not forget to add --recurse-submodules to `git pull` and `git fetch`, or simply configure it to happen automatically:

```sh
git config --global fetch.recurseSubmodules true
```

### Dependencies

The  code is built directly on a build machine (then all dependencies need to be installed, for the list see Docker/build/Dockerfile*) or using Docker (then only `docker` and `make` are needed)

### Building with Docker

To build with docker, make sure `docker` and  `make` are installed, and run

```sh
 make withdocker
 make withdocker TARGET=test
```

### Building directly on the box

First, Install all dependencies (see `build/docker/Dockerfile*` for the list of dependencies on either Ubuntu or Fedora).
Then, run make:

```sh
make
make test
```

## Build system structure

**We expect changes to the build system details so this doc is just a quick intro. For specifics and examples, see ../make/\*mk and all Makefiles.**

Build system uses tried-and-true `make` and related tricks. Generally, each dir with sources (or with subdirs) needs to have a Makefile. This Makefile need to have the following:

* The location of TOP of the repository, using  `TOP := $(shell git rev-parse --show-cdup)`
* A few variables to tell system what to build, e.g. `LIB := runtime`
* `include ${TOP}make/actions.mk`

Generally, the build system automatically builds dependencies and then does the following work:

### Scan subdirs and execute `make` there

Indicated by setting `SUBDIRS` to a list of subdirs. They are going to be scanned and `make` invoked in each of them

### Build an executable

Indicated by setting `EXEC` to the name of executable to build. When building, the system will take in account the values in `SOURCES`, `INCLUDES` and `LLIBS` makefile vars.

### Build a library

Indicated by setting `LIB` to the target lib, e.g. `LIB := runtime`

### Build and run tests

See ../tests/Makefile. This one is work in progress and will change to be more generic and provides uniform test execution

## CI/CD

TBD (does not exist yet)

## Source layout

* KM - Monitor code (executed as a user process on the host)
* runtime - libraries to be linked with payload (executed in a VM). Has subdirs with libraries used in the runtime (e.g. `./musl` and others)
* make - help / include files for the build system
* tests - tests sources

### Other repos

This repo uses musl C lib as a submodule (we use our own clone for that)
