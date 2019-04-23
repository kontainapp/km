# Rollout of Kubernetes cluster and other resources in the cloud

Scrips and pieces of code helping to deploy all needed resources (e.g. Kubernetes, container registry, security info) to supported clouds, so we can (later, from $(TOP)payloads) deploy payloads.

This functionality is similar to "install OS" - it kinds of lands / preps all resources to run actual workloads/.

* `./k8s` contains what is needed post-k8s rollout for it to be ready for payloads
* other (azure, minikybe, etc) have scripts to provision resources


TODO: minikube scripts; better management of kubectl context; and auto-deploy of `k8s/*`

