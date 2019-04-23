# Support for provisioning on minikube (text TODO)

Scripts and other fun to prepare all for running Kontain payloads on local laptop under Kubernetes, using minikube

## Prerequisities

* kubectl
  * https://kubernetes.io/docs/tasks/tools/install-kubectl/#install-kubectl-on-linux
* minikube
  * link TODO
* registry  - create and configure
  * info TODO

## Steps automated by scripts here

TODO

## Deploy to Kubernetes cluster

TODO
`make CLOUD=minikube ...`

Assuming you are in the top of KM repo

```bash
kubectl config use-context minikube
kubectl apply -f cloud/k8s/kvm-ds.yml
kubectl apply -k payloads/cloud/minikube

```
