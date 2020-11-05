# Cloud resource provisioning and Kubernetes cluster rollout

Subdirs here contain code used to deploy resources (e.g. Kubernetes, container registry or security group) to a specific cloud, e.g. `azure`.
After that we can deploy the actual payloads, with deplopyment code in ${TOP}/payloads dir.

Each cloud config variables are in related cloud_config.mk (e.g. `./azure/cloud_config.mk`)

* `./k8s` contains what is needed post-k8s rollout for it to be ready for payloads
* other (`azure`, `minikube`, etc) have scripts to provision resources
* __TODO__:
  * minikube scripts are missing
  * need better management of kubectl context

## Process

We expect the overall process to look like that:

* Login to the desired cloud (e.g. `az login`)
* Provision resources in a target cloud (e.g. `make -C azure resources`)
* Provision Kubernetes cluster (e.g. `make -C azure cluster`)
* Run application lifecycle:
  * Package the apps into docker images: `make -C ../ distro`
  * Publish images to the registry for the cloud:
    * Authenticate: `docker login` or `az acr login -n kontainkubecr` . This command is interactive
    * Publish: `make -C ../publish`
  * Deploy the app, e.g. `kubectl apply -k ../payloads/k8s/azure/python`.
   The deployment is based on *Kustomize* (and kubectl 1.13+ support for it), and python generic deploy script is expected in `../payloads/python/kustomization.yml`
  * Run, debug, etc..
  * Rinse, repeat
* clean up resources (if needed) with `make -C <cloud> cleancluster` or `make -C <cloud> cleanresources` (or `make -C <cloud> clean` for both)
* Rinse, repeat

# Known issues

The current code is based on CLI, quit simple but works. There is are two big pieces missing:

* it requires interacive login. Good for this stage but needs to be replaced
* it uses current branch name for hardcoded docker image tag - see kustomization.yml files
