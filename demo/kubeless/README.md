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

- `make -C kubeless function-controller` builds the function controller binary (`kubeless/bundles/kubeless_linux-amd64/kubeless-function-controller`).
- `make -C kubeless function-image-builder` builds docker image `kubeless-function-controller:latest` with the controller binary.

## Building HTTP Trigger

- `make http-controller-build` builds the HTTP trigger binary (`bundles/kubeless_linux-abd64/http-controller`).
- `make http-controller-image` builds docker image `kubeless-controller-manager:latest` with the HTTP trigger binary.

## Building Cronjob Trigger

**TBD**: Add this.

## Building Runtimes

**TBD**: Add this.

## Deploying into Kuberntes

*Note:* See `cloud/minikube/README.md` for details on running a virtual cluster on your local machine for testing.

Kubeless uses a YAML file (`kubeless/kubless.yaml`) to deploy into a Kubernetes cluster with `kubectl create -f <yaml file>`. `make -C kubeless all-yaml` will generate the default deployment files.

*Note*: The makefile uses a tool called `kubecfg` (which in turn uses a no longer being ddeveloped tool called `ksonnet-lib` (<https://github.com/ksonnet/ksonnet-lib>)) to convert `jsonnet` (<https://jsonnet.org/> template files into YAML files that can be used by `kubectl create -f`. This video gives an overview of `kubecfg` and `jsonnet`: <https://www.youtube.com/watch?v=zpgp3yCmXok>. That said, I haven't been able to figure out how to really use it.

Here is what I do instead.

- Build whatever images I want to test and make sure they are in minikube's container repository.
- `cp kubeless/kubeless.yaml my_kubeless.yaml`
- Edit `my_kubeless.yaml` to use the test images.
- `kubectl create ns kubeless`
- `kubectl create -f my_kubeless.yaml`
