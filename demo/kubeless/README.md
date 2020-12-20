# Notes on Building Kubeless

See <https://github.com/kubeless/kubeless/blob/master/docs/dev-guide.md> for information on building Kubeless.

Kubeless is divided across multiple sub-projects. Among them are:

- kubeless/kubeless <https://github.com/kubeless/kubeless> - Core engine.
- kubeless/runtimes <https://github.com/kubeless/runtimes> - Guest runtime.
- kubeless/http-trigger <https://github.com/kubeless/http-trigger> - Trigger for HTTP events.
- kubeless/cronjob-trigger <https://github.com/kubeless/cronjob-trigger> - Trigger for cron events.
- kubeless/*-trigger <https://github.com/kubeless/*-trigger> - Triggers for other events.

# Building kubeless-function-controller container
- `eval $(minikube docker-env)`
- `make -C kubeless function-controller`
- `make -C http-trigger http-controller-image`
# Building http-trigger container

- `make http-controller-build` builds `bundles/kubeless_linux-abd64/http-controller`.
- `make http-controller-image` builds docker image `kubeless-controller-manager:latest` with `bundles/kubeless_linux-amd64/http-controller`.
- `minikube cache reload http-trigger-controller:latest` updates minikube's docker to use updated image.

# Starting minikube test environment

- `minikube delete`
- `minikube start`
- `minikube addons enable ingress`
- `kubectl create ns kubeless`
- `kubectl create -f <local kubeless.yaml>