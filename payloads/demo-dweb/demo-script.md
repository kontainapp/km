# Kontain.app Demo #1 May 2019

This demo target to illustrate the key Kontain.app claims:

1. VM-level payloads isolation with start time and storage footprint much smaller than (inferior) Docker-based isolation
1. No source code changes for apps to run directly in Kontain VM (no guest OS)
1. No changes to operations - interoperability with Docker & Kubernetes. Run Kontain payload (microservice) in Kubernetes
1. Supporting complex apps - Python3 in KM. full Python interpreter with modules in Kontain VM, no OS.
1. Suitability for microservice - simple Python-based microservice in Kontain VM, as Kubernetes service

This document describes the *default* path to take during the demo.

## Prerequisites

* The demo hardware is expected to have KM repo checked out in `~/workspace/km`
* KM build is successful
* Docker v18.x should be installed for `make distro` and `make publish` to work. You can use docker-ce or moby-engine, they both install docker v18.x.
  * If docker is already installed, check version with `docker version -f 'Client: {{.Client.Version}}'`
  * For `docker-CE`, see [Docker installation info](https://docs.docker.com/install/linux/docker-ce/fedora/). As of the moment of this writing, they did not support Fedora30 so --releasever=29 needs to be passed to dnf
   * For `moby-engine`, just `dnf install` it.
* `make distro` should pass so km base container is available
* If docker image size comparison is needed, it's a good idea to pre-pull default Python images: `docker pull python`
* [kubectl (1.14+)](https://kubernetes.io/docs/tasks/tools/install-kubectl/#install-kubectl-on-linux) and Azure [az CLI (latest)](https://docs.microsoft.com/en-us/cli/azure/install-azure-cli-yum?view=azure-cli-latest) should be installed for Kubernetes/Azure part of the demo
* for meltdown demo, the hardware has to be Intel CPU supporting TSX instructions, and  meltdown mitigation should be turned off (`pti=off` on boot line)
* `jq` installed for pretty output formatting.
* Azure publish and Kubernetes deploy require interactive login:
```bash
az login
az acr login -n kontainkubecr
az aks get-credentials --resource-group kontainKubeRG --name kontainKubeCluster --overwrite-existing
```

## VM level isolation and start time; build from the same (unmodified) source

Goals:

* To show regular Linux app (web srv) converted into vm payload with single link command, no source changes
* To show an app (web srv) runs in Kontain VM
* To show start time in Kontain VM is much better that Docker container

Steps:

```bash
#!/bin/bash
# The rest of the script assumes that KM repo is available at ~/workspace/km/
# and KM binary at ~/workspace/km/build/km/km
# we don't use sh vars there so demo-time cut-n-paste from the text below does not depend on it

# Build simple web server - 'dweb'
cd ~/workspace/km/payloads/demo-dweb/dweb/; make clean
# open /show dweb.c (optonal: edit/change it). Then build:
make

# Note: 2 artifacts produced from the same .c and .o files
#   - dweb (Linux executable, shared libs)
#   - dweb.km (Kontain VM Payload)
file dweb dweb.km

# Run dweb.km as Kontain VM payload, and demo it (test->CPUID)
~/workspace/km/build/km/km ./dweb.km 8080

# Build Docker container with dweb
(cd ..; docker build -t dweb .)

# Show start time ('-x' to exit from server right awayzoom)
time docker run --rm  dweb  /dweb/dweb -x
time ~/workspace/km/build/km/km dweb.km -x

```

## Support complex workloads and microservices - Python3-based service

Goal:

* To show complex payload (Python interpreter) directly inside Kontain VM
* To demo an example of microservice inside Kontain

```bash
#!/bin/bash

# Build from the same code/object files:
cd ~/workspace/km/payloads/python; ./link-km.sh

# Show how python sees it's environment, in Linux and in km
cd cpython; ./python -c 'import os; print(os.uname())'
~/workspace/km/build/km/km ./python.km -c 'import os; print(os.uname())'

# Run a simple microservice - python http server with Restful API
# - Open /show ../scripts/micro_srv.py. (optional: edit/change it)
# - Run in terminal 1:
~/workspace/km/build/km/km ./python.km ../scripts/micro_srv.py

# In terminal 2
curl -s localhost:8080 | jq .  # shows os.uname()
curl -s -d "type=demo&subject=Kontain"  localhost:8080 | jq . # shows simple APi call

# Artifact size - compared off the shelf Python container with km-based one
# While we are here, let's compare container sizes for another payload, Python:
make -C ~/workspace/km/payloads/python distro
docker pull python
docker images | grep python | grep latest

```

## Operations

Goals:

* Demo compatibility with Docker/Kubernetes/cloud workflows - build/deploy/run on Kubernetes in Azure
* Show artifact size different

```bash
#!/bin/bash
# The rest of the script assumes that KM repo is available at ~/workspace/km/
# and KM binary at ~/workspace/km/build/km/km
# we don't use sh vars there so demo-time cut-n-paste from the text below does not depend on it

# Note: detailed guidance is in ~/workspace/km/cloud/README.md
make -C ~/workspace/km distro

# now push to docker registry and deploy the app
make -C ~/workspace/km/payloads/demo-dweb publish
kubectl apply -k ~/workspace/km/payloads/k8s/azure/dweb
kubectl get pod --selector=app=dweb  # make sure it shows as Running
kubectl port-forward `kubectl get pod --selector=app=dweb -o jsonpath='{.items[0].metadata.name}'` 8080:8080
# Manual: browser to localhost:8080, show test
# clean up
kubectl delete deploy kontain-dweb-deployment-azure-demo

# Optional: same demo for python microservice
make -C ~/workspace/km/payloads/python publish
kubectl apply -k ~/workspace/km/payloads/k8s/azure/python
kubectl get pod --selector=app=pykm  # make sure it shows as Running
kubectl port-forward `kubectl get pod --selector=app=pykm -o jsonpath='{.items[0].metadata.name}'` 8080:8080
# in terminal 2
curl -s localhost:8080 | jq .  # shows os.uname()
curl -s -d "type=demo&subject=Kontain"  localhost:8080 | jq . # shows simple APi call
# clean up
kubectl delete deploy kontain-pykm-deployment-azure-demo
```

## (optional) Local docker with our payloads

```bash
docker run -p 8080:8080 -t --rm --device /dev/kvm kontain/python-km /cpython/python.km -S "/scripts/micro_srv.py"
docker run -p 8080:8080 -t --rm --device /dev/kvm kontain/dweb-km dweb.km 8080
```

## Meltdown

We leave it to the end so Q&A follow will right away

Goals:

* To demo Meltdown attack being mitigated by running attacker as Kontain payloads
* open discussion / Q&A

Pre requisite:

* un-mitigate the kernel (done before demo :-)). Show cat `/proc/cmdline` with pti=off

```bash
#!/bin/bash

cd ~/workspace/km/payloads/meltdown

# get kernel address (optional)
(cd kaslr_offset/; sudo ./direct_physical_map.sh)

# show the break (and show run.sh if needed)
# Note: attack cli: 'physical_reader <kernel_address> <loc_from_secret_app>'
./run.sh

# same in KM
~/workspace/km/build/km/km physical_reader.km <phys_addr_from_secret_app> <direct_physical_map_from_kaslr>
```
