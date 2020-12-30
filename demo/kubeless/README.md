# Notes on Building Kubeless

See <https://github.com/kubeless/kubeless/blob/master/docs/dev-guide.md> for information on building Kubeless.

Kubeless is divided across multiple sub-projects. Among them are:

- kubeless/kubeless <https://github.com/kubeless/kubeless> - Core engine.
- kubeless/runtimes <https://github.com/kubeless/runtimes> - Guest runtime.
- kubeless/http-trigger <https://github.com/kubeless/http-trigger> - Trigger for HTTP events.
- kubeless/cronjob-trigger <https://github.com/kubeless/cronjob-trigger> - Trigger for cron events.
- kubeless/*-trigger <https://github.com/kubeless/*-trigger> - Triggers for other events.

Each of these are encapsulated in separate containers and are started in a single deployment called `kubeless-controller-manager` (`kubeless-controller-manager` is an example of a single deployment that starts multiple containers).

*Note:* For the rest of this note, assume there is a `kubeless` directory with each of the sub-projects checked out into subdirectories (for example, `kubeless` for the core, `http-trigger` for the HTTP trigger, etc.).

## Building the Core

We've created a fork of the kubeless core in github. See <https://github.com/kontainapp/kubeless>.

- `make -C kubeless function-controller` builds the function controller docker image (`kubeless-function-controller`).
- `make -C kubeless function-image-builder` builds the docker image `kubeless-function-controller:latest` used to build function images.

## Building HTTP Trigger

- `make http-controller-build` builds the HTTP trigger binary (`bundles/kubeless_linux-abd64/http-controller`).
- `make http-controller-image` builds the docker image `http-trigger-controller:latest`.

## Building Cronjob Trigger

**TBD**: Add this. (I'm not sure we'll need to do this).

## Building Runtimes

Kubeless uses a separate git repo for runtime. We've created a fork of the kubeless runtimes repo (<https://github.com/kontainapp/runtimes>). The developer guide in the repo gives a good overview of the process to develop a runtime.

Kubeless assumes a container registry

<https://github.com/kubeless/kubeless/blob/master/docs/building-functions.md>

Individual functions are wrapped in their own dedicated container. These containers are tagged with the SHA of the function and it's dependencies (if any). This allows kubeless to recognize when a function already exists in the container registry it is using.

## Deploying into Kuberntes

*Note:* See `cloud/minikube/README.md` for details on running a virtual cluster on your local machine for testing.

Kubeless uses a YAML file (`kubeless/kubless.yaml`) to deploy into a Kubernetes cluster with `kubectl create -f <yaml file>`. `make -C kubeless all-yaml` will generate the default deployment files.

*Note*: The makefile uses a tool called `kubecfg` (which in turn uses a no longer being ddeveloped tool called `ksonnet-lib` (<https://github.com/ksonnet/ksonnet-lib>)) to convert `jsonnet` (<https://jsonnet.org/> template files into YAML files that can be used by `kubectl create -f`. This video gives an overview of `kubecfg` and `jsonnet`: <https://www.youtube.com/watch?v=zpgp3yCmXok>. That said, I haven't been able to figure out how to really use it.

For testing you typically need to configure kubeless to use your container(s) in the kubeless control pod instead of the released one in Github. The easiest way to do this is make a copy of `kubeless.yaml` and replace the appropriate `image:` entries.

Here is what I do instead using `minikube docker-env`.

Initial setup:

- `minikube delete ; minikube start` # start from scratch
- `minikube addons enable ingress` # needed for kubeless HTTP trigger
- `kubectl create ns kubeless`

Development loop

- Rebuild container(s) I'm testing.
- `kubectl create -f <my yaml file>`
- Do work
- Remove any kubeless items create (`kubectl delete` hangs if functions, triggers, etc. exist)
- `kubectl create -f <my yaml file>`
- Go to 'Rebuild containter(s)

## Playing with runtimes

```
KUBECFG_JPATH=$(pwd)/ksonnet-lib:$(pwd)/../runtimes kubecfg show  -o yaml km_kubeless.jsonnet > km_kubeless.yaml
```

## Design Notes

Kubeless uses two mechanisms to persist data:

- Kubernetes cluster configuration database (`etcd`).
- Container repository (configurable, defaults to Docker Hub). Container types
  - kubeless-function-controller - main controller for kubeless. Listens for changes to functions.
  - Trigger controllers http-trigger-controller, cronjob-trigger-controller, etc. - trigger controllers. Listens for changes to triggers. Will delete trigger if function is deleted.
  - function-image-builder -  Builds function containers.
  - function containers - Runs a single function.
