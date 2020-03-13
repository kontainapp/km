# Kontain Payloads

This directory keeps the collection of payloads we explicitly converted to work as KM payloads.
We also keep here the `./k8s` dir with the code to customize payload deployments to Kubernetes in different clouds (e.g. Azure).


* Build
  * `make all` This does initial pull from repos, e.g. `cpython`, then configures and builds.This command will only pull / configure / build payloads once.
  * `make build` also pulls only onces, but configures and builds payloads on every run
* Package and publish:
  * Payloads are distributed as Docker images
  * `make runenv-image` creates Docker images (all images if run from the top, or specific image if run in a specifif dir)
    * *Prerequisites*
      * Docker needs to be install and configured
    * Doing `make runenv-image` from top level will build payload Docker images.
* `make publish` will push them to container registry. You need to be logged in the proper registry (Azure ACR, dockerhub, etc.. - see below)
  * $(TOP)/make/`locations.mk` defines the CLOUD variable as `azure`, so by default, it will try to push to Azure Container Registry.
  * Registry name is defined in `./k8s/azure/kustomization.yml`.
  * You can redefine where to push when running make, e.g.  `make CLOUD=minikube publish` will push to registry defined in `./k8s/cloud/minikube/kustomization.yml`.
* `kubectl apply -k k8s/<cloud>` to deploy to `<cloud>`, e.g `kubectl apply -k k8s/azure`
  * This assunes your kubectl context is set to point to the correct cloud


Note that the actual deployment config yamls are in `./payloads/...` dirs , e.g. `./payloads/python/pykm-deployment.yml`, and `./k8/...` keeps customizations for which are applied on the top of payload .yml files.
