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
