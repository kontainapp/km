# Kontain Deployment for Kubernetes

Deployment for Kontain Runtime for Kubrenetes. This was modelled on kata deployment, but it is greatly simplified.
Currently, only CRI-O container runtime is supported.

`Makefile` builds the container `kontainapp/k8s-deploy:latest` and pushes it to DockerHub. Relevent targets:
* `make runenv-image` builds the delpoyment container.
* `make push-runenv-image` pushes the deployment image to the DockerHub registry.

The deployment container includes  KM build artifacts `/opt/kontain` as well as the script `k82-deploy.sh`. The DeamonSet is deployed
into a kubernetes cluster with the command `kubectl apply -f k8s-deploy.yaml`.
