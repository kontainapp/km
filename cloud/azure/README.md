# Support for provisioning on Azure

Scripts and other fun to prepare all for running Kontain payloads on Azure under Kubernetes.
Also has support for creating VM for Openwhisk demo/tests (`*ow` targets)

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

## Openwhisk-related targets

Use `make help` for help.

* `make ow` to  build Vms.
  * Note: The script is not tested when  the private key is password protected. ssh private key from ~/.ssh/id_rsa is used for the duration of `git clone` commands.
  * for changes to the VM names list, directly edit VMS var in `create_openwhisk_vms.sh`
  * VMs are named `name-openwhisk-kontain`, per *create_openwhisk_vms.sh*) e.g. *km-openwhisk-kontain* VM with km-openwhisk-kontain.westus.cloudapp.azure.com DNS name.
  * VMs have the same size (D3V3, about $200/mo), and all 3 have all for openwhisk installed. This is done do simplify the scripts
  * VMs also have ssh configured per ./config source dir (.pub files are added to authorized keys and config is copied).
  * VMs have Openwhisk and other stuff per [Openwhisk testing instructions](https://github.com/kontainapp/openwhisk-performance/blob/master/readme.md) installed.
* `make listow` to list Vms names and IPs
* `make clearow` to delete the *-openwhisk-kontain VMS

To stop all vms so they are not billed to us, run `apply_openwhisk_vms.sh deallocate` directly (no `make` wrapper). To restart, `apply_openwhisk_vms.sh start` (`apply_openwhisk_vms.sh help` for what the script can do )

All targets assume `make login` succeded.

## Debugging CI

To debug an issue that only happens on CI, we can launch a VM with the same kernel.

```bash
# To create the VM. IP address will be printed.
VM_IMAGE="Canonical:UbuntuServer:16.04-LTS:latest" $TOP/cloud/azure/azure_vm.sh create <name>

# To look up the IP address
$TOP/cloud/azure/azure_vm.sh ls <name>

# ssh into the machine and clone the km repo and sync the submodule.
ssh kontain@<ip>

# To delete when finished.
$TOP/cloud/azure/azure_vm.sh delete <name>
```
