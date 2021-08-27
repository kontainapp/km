# Demo for Kontain Kubernetes Install

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

The command to create a minikube cluster that uses the CRI-O container runtime is:
- `minikube start --container-runtime=cri-o`

I needed to build the v1.22.0 version of minikube to get working `cri-o` runtime.
That version of minikube source requires golang 1.16 or greater to build.
