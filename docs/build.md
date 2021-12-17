# Build, test, CI and sources layout

## Setting out new Fedora machine

Before anything can be built and tested on a local Fedora machine we need to install and configure a few components.
This is usually done once, after the machine is freshly installed.

Generally we install using dnf as much as possible,
which makes it easy to maintain current versions of packages via `sudo dnf upgrade`.

We assume the user has administrator privileges, i.e. is a member of group wheel.

### sudo

Some scripts need to be run as root.
To make things easier it is best to enable password-less `sudo`.
Edit file `/etc/sudoers` using `sudo visudo`,
comment out the line `%wheel        ALL=(ALL)       ALL` and uncomment out the line
`%wheel  ALL=(ALL)       NOPASSWD: ALL`.
Here is the relevant fragment of the file:

```txt
   ...

## Allows people in group wheel to run all commands
# %wheel        ALL=(ALL)       ALL

## Same thing without a password
%wheel  ALL=(ALL)       NOPASSWD: ALL

## Allows members of the users group to mount and unmount the
## cdrom as root
# %users  ALL=/sbin/mount /mnt/cdrom, /sbin/umount /mnt/cdrom

   ...
```

### Upgrade the system

To make things up to date, run

```bash
sudo dnf upgrade --refresh -y
```

This could take a while as many packages are downloaded and installed.
The system most likely needs to reboot after that to use new kernel,
however that could wait till next step as docker requires reconfiguring kernel parameters.

### Docker

Install

```bash
sudo dnf install -y moby-engine
```

*** Additional configuration if you are using fedora31 or fedora32 or fedora33 (moby-engine on fedora34 uses cgroups v2) ***
There are a bunch of glitches between latest docker's moby-engine and fedora 31/32/33 configurations.
Here is a good summary / instruction for fixes: https://fedoramagazine.org/docker-and-fedora-32/.

In short, you may run into cgroups v1 vs v2 issue on fedora31 or fedora32 or fedora33.
To enable cgroups v1 on fedora31 or fedora32 or fedora33 (but ignore if using fedora34):

```bash
sudo grubby --update-kernel=ALL --args="systemd.unified_cgroup_hierarchy=0"
```

Also there are firewall rules blocking docker0. To fix:

```bash
sudo firewall-cmd --permanent --zone=trusted --add-interface=docker0
sudo firewall-cmd --permanent --zone=FedoraWorkstation --add-masquerade
sudo systemctl restart firewalld
```

To enable docker for unprivileged user:

```bash
sudo usermod -aG docker root
sudo usermod -aG docker $(id -un)
```

In order for these changes to take effect you need to exit and login again.

Enable docker start on system boot, and start it:

```bash
sudo systemctl enable docker
sudo systemctl start docker
```

Check everything is OK with docker:

```bash
docker ps
```

At this point it makes sense to reboot `sudo reboot`.

### Various - make, Azure CLI, kubectl

```bash
sudo sh -c 'echo -e "[azure-cli]
name=Azure CLI
baseurl=https://packages.microsoft.com/yumrepos/azure-cli
enabled=1
gpgcheck=1
gpgkey=https://packages.microsoft.com/keys/microsoft.asc" > /etc/yum.repos.d/azure-cli.repo'
```
```bash
sudo sh -c 'echo -e "[kubernetes]
name=Kubernetes
baseurl=https://packages.cloud.google.com/yum/repos/kubernetes-el7-x86_64
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://packages.cloud.google.com/yum/doc/yum-key.gpg https://packages.cloud.google.com/yum/doc/rpm-package-key.gpg" > /etc/yum.repos.d/kubernetes.repo'
```

```bash
sudo dnf install -y git python3-cffi make azure-cli kubectl
```

### All of the above in one script for easy cut and paste

```bash
sudo dnf upgrade --refresh -y
sudo dnf install -y moby-engine
sudo grubby --update-kernel=ALL --args="systemd.unified_cgroup_hierarchy=0"
sudo firewall-cmd --permanent --zone=trusted --add-interface=docker0
sudo firewall-cmd --permanent --zone=FedoraWorkstation --add-masquerade
sudo systemctl restart firewalld
sudo usermod -aG docker root
sudo usermod -aG docker $(id -un)
sudo systemctl enable docker
sudo sh -c 'echo -e "[azure-cli]
name=Azure CLI
baseurl=https://packages.microsoft.com/yumrepos/azure-cli
enabled=1
gpgcheck=1
gpgkey=https://packages.microsoft.com/keys/microsoft.asc" > /etc/yum.repos.d/azure-cli.repo'
sudo sh -c 'echo -e "[kubernetes]
name=Kubernetes
baseurl=https://packages.cloud.google.com/yum/repos/kubernetes-el7-x86_64
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://packages.cloud.google.com/yum/doc/yum-key.gpg https://packages.cloud.google.com/yum/doc/rpm-package-key.gpg" > /etc/yum.repos.d/kubernetes.repo'
sudo dnf install -y git python3-cffi make azure-cli kubectl
```

Don't forget to exit and login again so that the new group membership takes effect.
Or simply reboot the system and login.

### Source tree and build environment

These are quick steps to set up build environment.
See "Build and test" below for full description.

Sources are maintained in github in private repo.
In order to get access to the repo we need to create public key and import it into github.
Public/private key pair is create by `ssh-keygen`.
The content of `~/.ssh/id_rsa.pub` file needs to be entered into `https://github.com/settings/keys` page.
Githib will requests identity confirmation.

It helps to configure local git settings and aliases in `~/.gitconfig`:

```txt
# This is Git's per-user configuration file.
[user]
   name = You Name
   email = you_name@kontain.app

[alias]
        ls = log --graph --pretty=format:'%C(yellow)%h%Creset%C(cyan)%C(bold)%d%Creset %s (%C(cyan)%cr%Creset %C(green)%ce%Creset)'

[help]
   autocorrect = 1
```

Clone the source repo, make sure to update submodules:

```bash
git clone git@github.com:kontainapp/km.git
cd km
git submodule update --init
```

To configure local build environment and docker build environment we need to fetch a build environment image:

```bash
make -C cloud/azure login
make -C tests pull-buildenv-image
make -C tests buildenv-local-fedora
```

The last command will install local packages needed for build and test.

Now we are ready for local build and test:

```bash
make -j
make -C tests test
```

Or we can do build and test in docker containers in the same way CI system does it in the cloud.
Note if you build locally before, you need to clean first:

```bash
make clean
make -C container-runtime clobber
```

```bash
make pull-buildenv-image
make withdocker -j
make -C payloads/python
make -C payloads/python testenv-image
make -C payloads/node
make -C payloads/node testenv-image
make -C payloads/java
make -C payloads/java testenv-image
make test-withdocker
```

Once you have kontain source code in your working directory you need to install podman and then configure podman and docker to
use krun and km to run payloads.

```
sudo container-runtime/podman_config.sh
sudo container-runtime/docker_config.sh
```

Now docker and podman will try to use krun and km when the "--runtime krun" argument is supplied with "docker run" or "podman run".
Without --runtime, runc will be the default container runtime.

The kontain makefile clean target cleans out /opt/kontain/bin so you will find that docker or podman run fail because
these two programs are missing.
Just rebuild them.

## Following are various tools

### Visual Studio Code

```bash
sudo rpm --import https://packages.microsoft.com/keys/microsoft.asc
sudo sh -c 'echo -e "[code]
name=Visual Studio Code
baseurl=https://packages.microsoft.com/yumrepos/vscode
enabled=1
gpgcheck=1
gpgkey=https://packages.microsoft.com/keys/microsoft.asc" > /etc/yum.repos.d/vscode.repo'
```

```bash
sudo dnf install -y code
```

### Google chrome

```bash
sudo sh -c 'echo -e "[google-chrome]
name=google-chrome
baseurl=http://dl.google.com/linux/chrome/rpm/stable/x86_64
enabled=1
gpgcheck=1
gpgkey=https://dl.google.com/linux/linux_signing_key.pub" > /etc/yum.repos.d/google-chrome.repo'
```

```bash
dnf install -y google-chrome-stable
```

## Coding Style

We encourage the use of Visual Studio Code (VS Code) IDE. Our VS Code workspace (`km_repo_root/.vscode/km.code-workspace`) is configured to automatically
run clang-format on file save.

In addition, our CI runs clang-format on the `km` and `tests` directory for every PR, merge with master, and nightly build.q

Some of the style points we maintaining C which are not enforced by clang:

* never use single line `if () statement;` - always use `{}`,i.e. `if() {statement;}`
* single line comments are usually `//`, multiple lines `/* ... */`
* more to be documented

In Python we follow [Pep 8](https://www.python.org/dev/peps/pep-0008/)

## Build and test

### Cloning the repo

We use git submodules, so do not forget to init/update them. Assuming your credentials are in order, here is how to bring the stuff in:

```sh
# Force submodule traversal for all git repos
git config --global submodule.recurse true
# Clone KM
git clone git@github.com:kontainapp/km.git
cd km
# In order for traverse to work, init has to happen one time
git submodule update --init
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
1. Create personal Security Principal for non-interactive logins: `az ad sp create-for-rbac -n "https://your_alias_noninteractive" --role contributor --years N`
1. Save the results in local file, e.g. *~/.ssh/az_$(whoami)_noninteractive*. Set chmod 400 on the file. This is secret data, so **DO NOT PUBLISH AND DO NOT SAVE TO GIT**.
1. Add credential to .bash_profile - code below (modify the file var first). If you use other shells, modify the script accordingly.
1. `source ~/.bash_profile`
1. **Test the login:** `make -C cloud/azure login-cli`

Code to convert `az ad sp create-for-rbac` results to env variables needed by Makefiles:

```sh
t_name="http://az_$(whoami)_noninteractive"
file=~/.ssh/$t_name
az ad sp create-for-rbac -n "$t_name" --role contributor --years 3 | tee $file
chmod 400 $file
echo -e "\n# Azure secret for non-interactive login $t_name. Created $(date)" >> ~/.bash_profile
cat $file  | jq -r '"export SP_APPID=\(.appId)",
                    "export SP_DISPLAYNAME=\(.displayName)",
                    "export SP_NAME=\(.name)",
                    "export SP_PASSWORD=\(.password)",
                    "export SP_TENANT=\(.tenant)"' \
               >> ~/.bash_profile
```

### Pull the 'buildenv' image from Azure Container Registry

We maintain docker build environment docker images that contain all build prerequisites.
There is one build environment image for km, and one for each payload.
They are called `kontain/buildenv-km-fedora` for km and `kontain/buildenv-`_payload_`-fedora` (e.g. `kontain/buildenv-python-fedora`).
The km one is maintained by `Makefile` in `tests` directory, the payload ones in corresponding payload directories.
Top directory goes down the tree recursively.

To fetch a buildenv-image for km only:


```sh
make -C tests pull-buildenv-image
```

And for specific payload (e.g. python):

```sh
make -C python pull-buildenv-image
```

```sh
make pull-buildenv-image
```

from the top will get all of the build environments.

#### build the 'buildenv' image locally using Docker

An alternative way is to build 'build environment' images locally. Similarly to the above, this could be done in a specific directory (tests for km), or recursively to build them all.

For km build environment:

```sh
make -C tests buildenv-image  # this will take VERY LONG time
```

and so forth.

#### Install dependencies on the local machine (after buildenv image is available)

Build environments also used to install the necessary packages and dependencies on the local system.
Again it is maintained per directory in the same way as above.

For km only:


```sh
make -C tests buildenv-local-fedora  # currently supported on fedora only !
```

Or for all of the payloads and km:

```sh
make buildenv-local-fedora  # currently supported on fedora only !
```

### Building with Docker

#### Building

To build with docker, make sure `docker` and  `make` are installed, and buildenv image is available (see section about this above) and run the commands below.

*Building with docker* will use a docker container with all necessary pre-requisites installed; will use local source tree exposed to Docker via a docker volume, and will place the results in the local *build* directory - so the end result is exactly the same as regular 'make', but it does not require pre-requisites installed on the local machine. One the the key use cases for build with docker is cloud-based CI/CD pipeline.

```sh
 make -C tests .buildenv-local-lib # one time to prep host machine (see tests/readme.md)
 make withdocker
 make withdocker TARGET=test
```

We use `make -C tests .buildenv-local-lib` even for dockerized builds because of the following:

* We need a writable /opt/kontain/runtime on the host for dynlinker (libc.so). We generate this on runtime build, and then volume-mount and use it during the rest of the build.
* Creating this dir needs `sudo`, so we piggyback on the target above to also create writable /opt/kontain/runtime/.
* Instead of the above, you can simply do `make -C tests /opt/kontain/runtime` and get the box prepared for dockerized build

### Building directly on the box

First, Install all dependencies using `make -C tests buildenv-local-fedora` make target (specific steps are above, and detailed  explanation of images is `in docs/image-targets.md`

Then, run make from the top of the repository:

```sh
make
make test
```

## Build system structure

At any dir, use `make help` to get help on targets. Or, if you use bash, type `make<tab>` to see the target list.

Build system uses tried-and-true `make` and related tricks. Generally, each dir with sources (or with subdirs) needs to have a Makefile. Included makefiles are in `make/*mk`.

This Makefile need to have the following:

* The location of TOP of the repository, using  `TOP := $(shell git rev-parse --show-toplevel)`
* A few variables to tell system what to build (e.g. `LIB := runtime`), and what to build it from (`SOURCES := a.c, x.c …`), compile options (`COPTS := -Os …`) and include dirs (`INCLUDES := ${TOP}/include`)
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

This builds `kontainapp/runenv-<payloads>` and run a test.

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

```sh
# deploy `kontaind`
make -C cloud/k8s/kontaind runenv-image
make -C cloud/k8s/kontaind push-runenv-image
make -C cloud/k8s/kontaind install
```

#### Push and Run demo apps

From the top of the repo, build and push demo containers:

```sh
make demo-runenv-image
make push-demo-runenv-image
```

* To deploy KM-based apps:  `kubectl apply -k ./payloads/k8s/azure/python/`
* To clean up: `kubectl delete -k ./payloads/k8s/azure/python/`

:point_right: **Note** we use `kustomize` support in kubectl, it's discussion is beyond the scope of this doc, especially for demo. See `payloads/demo_script.md` for more info on the demo

### CI/CD

We use Gitgub Action for for CI/CD. See
`.github/workflows/km-ci-workflow.yaml` for configuration and `ci_pipeline.md` doc for info,
including FAQ (e.g how to run CI containers manually)

### Coverage

To maintain the quality of code, we implemented code coverage for `km`.
First, we need to build a version of km with extra flag `--coverage`.

```sh
make -C km coverage
```

The newly built km coverage binary will be located at
`$TOP/build/km/coverage/km` along with the object files and `.gcno` filed
used for testing coverage. With the compile flags, `km` now outputs extra
files to track the coverage. We run the same tests using this binary to
obtain the test coverage. For extra details, see `gcov(1)` and `gcc(1)`.

To run tests for coverage analysis:

```sh
make -C tests coverage
```

Note: the `--coverage` flag will cause km to output `.gcda` files that's used
in the coverage analysis. The path which the files is outputted to is
configured at compile time through flags and by default configured to the
same place where the object files resides (`$TOP/build/km/coverage`). There
is no easy way to change this path at runtime.

#### Coverage with docker

We also have the option to run coverage tests inside the container. First, we
need to build the km binary inside the container.

```sh
make -C km withdocker TARGET=coverage
```

The we can run the tests:

```sh
make -C tests coverage-withdocker
```

Or we can run both steps together with:

```sh
make withdocker TARGET=coverage
```

Note, because output of coverage files like `.gcda` files are configured at
compile time, and because the filesystem structure inside the buildenv and on
the host are different, users need to do both compile + run coverage either
on the host, or `withdocker`. Doing one on the host and another `withdocker` will
fail, and there is no easy way to support the workflow, because the absolute
path is hardcoded at compile time.

#### Coverage with k8s

Testenv is designed to contain both versions of `km` binary. The regular `km`
is located under `/opt/kontain/bin/km` and the coverage version `km` is
located under `/opt/kontain/bin/coverage/km`. Testenv doesn't include any
source files, by design, so the analysis of coverage using `gcov` needs to
take place on the host, where it can have access to source code. Therefore,
scripts will copy the `.gcda` files onto the host. To run:

```sh
make -C tests coverage-withk8s
```

#### Upload coverage

Coverage on the CI pipeline will generates a list of reports (html files).
These will be uploaded to
[km-coverage-report](https://github.com/kontainapp/km-coverage-report). There
is an option to manually upload these reports, if users choose to. `make -C
tests upload-coverage-manual`. Note, we tag each commits using the
`IMAGE_VERSION`, so it needs to be set to some unique id of your choice,
compliant with 'git tag' format. The repo is configured to use Github
Page to serve these html files. To access the latest reports, use the
following url: https://kontainapp.github.io/km-coverage-report. To access
older reports, checkout the repo and search using the tags.

To see coverage for your tag, run the following:

```bash
cd ~/workspace
git clone -b **your_tag** git@github.com:kontainapp/km-coverage-report.git
google-chrome   km-coverage-report/index.html
```

or check out the repo once and simply run in it's dir:

```bash
git fetch -p
get checkout **your tag**
google-chrome index.html
```

### Other repos

This repo uses a set of submodules - e.g. musl C lib and bats testing suite

=== END OF DOCUMENT ===
