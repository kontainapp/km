# Demo for Kontain Kuberbetes Install

## Prerequisites

* Kubernetes Cluster with Kontain Runtime Deployed (see cloud/k8s/deploy)

## Demo Procedure

* Start test pod: `kubectl apply -f test.yaml`
* Demonstrate Kontain runtime in control
```
$ kubectl exec -it kontain-test-app-5c77dcf745-h7lbn -- uname -r
5.13.7-100.fc33.x86_64.kontain.KVM
```

## Minikube Notes

To create a minikube cluster that used the CRI-O container runtime use:
`minikube start --container-runtime=cri-o`.

I needed to build the v1.22.0 version of minikube to get working `cri-o` runtime.
That verion of minikube source requires golan 1.16 or greater.
