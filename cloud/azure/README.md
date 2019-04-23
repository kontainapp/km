# Support for provisioning on Azure

Scripts and other fun to prepare all for running Kontain payloads on Azure under Kubernetes.

## Prerequisities

* kubectl
  * https://kubernetes.io/docs/tasks/tools/install-kubectl/#install-kubectl-on-linux
* `az` (azure) CLI
  *  https://docs.microsoft.com/en-us/cli/azure/install-azure-cli-yum?view=azure-cli-latest
* Azure credentials (TODO: to use tokens instead of interactive login; for now my personal kontain.app account is used).
  * az login
  * az acr login -n kontainkubecr

## Steps automated by scripts here

Cloud config and cluster provision

* Create and configure Azure Contaner Registry (ACR)
* Create and configure Service Principal (SP)
* Create Kubernetes cluster with SP (or auto-generate SP based on pre-defined ssh keys)
* grant cluster's  SP access to ACR

Cluster config:

* Deploy kvm-device-plugin to the cluster to allow /dev/kvm access from container
* future:
  * runtime config (maybe a part of cluster deploy ? it depends)
  * deploy KM (for now keep it in payload images)

App:

* deploy a demo app (still `TODO`)

Since we require KVM support, we can only use a subset of Azure instances. We use what seems to be the cheapest from this set is `Standard_D2s_v3` (still an overkill, but have to use it due to KVM availability)


## Deploy to Kubernetes cluster

Assuming you are in the top of KM repo

```bash
kubectl apply -f cloud/k8s/kvm-ds.yml
kubectl apply -k payloads/cloud/azure

```
