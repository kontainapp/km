# Kontain.app Demo #1 May 2019

This demo target to illustrate the key Kontain.app claims:

1. VM-level payloads isolation with start time and storage footprint much smaller than (inferior) Docker-based isolation
1. No source code changes for apps to run directly in Kontain VM (no guest OS)
1. No changes to operations - interoperability with Docker & Kubernetes. Run Kontain payload (microservice) in Kubernetes [ Question: should we do it instead for python microservice]
1. Supporting complex apps -  Python3 in KM. full Python interpreter with modules in Kontain VM, no OS)
1. Suitability for microservice - simple Python-based microservice in Kontain VM, as Kubernetes service

This document describes the *default* path to take during the demo.

## Prerequisites

* The demo hardware is expected to have KM repo checked out in `~/workspace/covm` (change `repo=...` in the text below if the location is different()
* KM build is successful
* `make distro` should also pass so km base container is available
* [kubectl (1.14+)](https://kubernetes.io/docs/tasks/tools/install-kubectl/#install-kubectl-on-linux) and Azure [az CLI (latest)](https://docs.microsoft.com/en-us/cli/azure/install-azure-cli-yum?view=azure-cli-latest) should be installed for Kubernetes/Azure part of the demo
* for meltdown demo, the hardware has to be intel supporting TSX instructions, and  meltdown mitigation should be turned off (`pti=off` on boot line)
* `jq` installed for demo formatting
* Azure publish and Kubernetes deploy require interactive login to Azure
  * `az login`
  * `az acr login -n kontainkubecr`
  * `az aks get-credentials --resource-group kontainKubeRG --name kontainKubeCluster --overwrite-existing`

## VM level isolation and start time; build from the same (unmodified) source

Goals:

* To show regular Linux app (web srv) converted into vm payload with single link command, no source changes
* To show an app (web srv) runs in Kontain VM
* To show start time in Kontain VM is much better that Docker container

Steps:

```bash
#!/bin/bash
# Set shell var to make life easier
repo=~/workspace/covm/; km=$repo/build/km/km

# Build simple web server - 'dweb' pp
cd $repo/payloads/demo-dweb/dweb/; make clean
make

# Note: 2 artifacts produced from the same .c and .o files
#   - dweb (Linux executable, shared libs)
#   - dweb.km (Kontain VM Payload)
file dweb dweb.km

# Run dweb.km as Kontain VM payload, and demo it (test->CPUID)
$km ./dweb.km 8080

# Build Docker container with dweb
(cd ..; docker build -t dweb .)

# Show start time
time docker run --rm  dweb  /tmp/dweb -x
time $km dweb.km -x

# optonal: change dweb.c / rebuild / demo
make; $km ./dweb.km 8080
```

## Support complex workloads and microservices - Python3-based service

Goal:

* To show complex payload (Python interpreter) directly inside Kontain VM
* To demo an example of microservice inside Kontain

```bash
#!/bin/bash
repo=~/workspace/covm/; km=$repo/build/km/km

# Build from the same code/object files:
cd $repo/payloads/python; ./link-km.sh

# Run a simple microservice - python http server with Restful API

# in terminal 1:
cd cpython; $km ./python.km ../scripts/micro_srv.py

# in terminal 2
curl -s localhost:8080 | jq .  # shows os.uname()
curl -s -d "type=demo&subject=Kontain"  localhost:8080 | jq . # shows simple APi call

# optional:  show/edit/rerun.  The script is ../scripts/micro_srv.py
```

## Operations

Goals:

* Demo compatibility with Docker/Kubernetes/cloud workflows - build/deploy/run on Kubernetes in Azure
* Show artifact size different

```bash
#!/bin/bash
repo=~/workspace/covm/; km=$repo/build/km/km

# Note: detailed guidance is in $repo/cloud/README.md
make -C $repo distro
make -C $repo publish

kubectl apply -k $repo/payloads/k8s/azure/python
kubectl get pod
kubectl port-forward <pod-id> 8080:8080

# in terminal 2, same commands
curl -s localhost:8080 | jq .  # shows os.uname()
curl -s -d "type=demo&subject=Kontain"  localhost:8080 | jq . # shows simple APi call

# cleanup and deploy dweb
kubectl delete deploy kontain-pykm-deployment-azure-demo
kubectl apply -k $repo/payloads/k8s/azure/dweb
kubectl get pod
kubectl port-forward <pod-id> 8080:8080

# browser to localhost:8080, show test

# clean up
kubectl delete deploy kontain-dweb-deployment-azure-demo
```

## (optional) Local docker with our payloads

```bash
docker run -p 8080:8080 -t --rm --device /dev/kvm kontain/python-km /cpython/python.km "/scripts/micro_srv.py"
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
repo=~/workspace/covm/; km=$repo/build/km/km

cd $repo/payloads/meltdown

# get kernel address (optional)
(cd kaslr_offset/; sudo ./direct_physical_map.sh)

# show the break (and show run.sh if needed)
# Note: attack cli: 'physical_reader <kernel_address> <loc_from_secret_app>'
./run.sh

# same in KM
$km physical_reader.km <kernel_address> <loc_from_secret_app>
```
