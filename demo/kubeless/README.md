# Notes on Building Kubeless

See <https://github.com/kubeless/kubeless/blob/master/docs/dev-guide.md> for information on building Kubeless.

Kubeless is divided across multiple sub-projects. Among them are:

- kubeless/kubeless <https://github.com/kubeless/kubeless> - Core engine.
- kubeless/runtimes <https://github.com/kubeless/runtimes> - Guest runtime.
- kubeless/http-trigger <https://github.com/kubeless/http-trigger> - Trigger for HTTP events.
- kubeless/cronjob-trigger <https://github.com/kubeless/cronjob-trigger> - Trigger for cron events.
- kubeless/*-trigger <https://github.com/kubeless/*-trigger> - Triggers for other events.

Each of these are encapsulated in separate containers for deployment into a kubernetes cluster.

*Note:* For the rest of this note, assume there is a `kubeless` directory with each of the sub-projects checked out into subdirectories (for example, `kubeless` for the core, `http-trigger` for the HTTP trigger, etc.).

## Building the Core

- `make -C kubeless function-controller` builds the function controller binary (`kubeless/bundles/kubeless_linux-amd64/kubeless-function-controller`).
- `make -C kubeless function-image-builder` builds docker image `kubeless-function-controller:latest` with the controller binary.

## Building HTTP Trigger

- `make http-controller-build` builds the HTTP trigger binary (`bundles/kubeless_linux-abd64/http-controller`).
- `make http-controller-image` builds docker image `kubeless-controller-manager:latest` with the HTTP trigger binary.

## Deploying into Kuberntes

*Note:* See `cloud/minikube/README.md` for details on running a virtual cluster on your local machine for testing.

Kubeless uses a YAML file (`kubeless/kubless.yaml`) to deploy into a Kubernetes cluster.

A tool called `ksonnet-lib` (<https://github.com/ksonnet/ksonnet-lib>) converts  `ksonnet-lib` specific template files into Kubernetes YAML files that define Kubeless to the cluster. __TODO__: Understand how `ksonnet-lib` works.

- `kubectl create ns kubeless`
- `kubectl create -f kubeless/kubeless.yaml`
