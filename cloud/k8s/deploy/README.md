# Kontain Deployment for Kubernetes

Deployment for Kontain Runtime for Kubernetes. This is modeled on the kata containers deployment, but it is simplified.
Both containerd and CRIO are supported by this script.

`Makefile` builds the container `kontainapp/runenv-k8s-deploy:latest` and pushes it to DockerHub. Relevant targets:
* `make runenv-image` builds the delpoyment container.
* `make push-runenv-image` pushes the deployment image to the DockerHub registry.

The deployment container includes  KM build artifacts `/opt/kontain` as well as the script `k82-deploy.sh`. The DaemonSet 
is deployed into a kubernetes cluster with the command `kubectl apply -f k8s-deploy.yaml`.

Kontain is exposed as a Runtime Class named 'kontain'. Users specify the kontain runtime with `runtimeClassName: kontain`
in a container 'spec'.  For example:

```
...
    spec:
      runtimeClassName: kontain
      containers:
      - name: kontain-test-app
        image: busybox:latest
        command: [ "sleep", "infinity" ]
        imagePullPolicy: IfNotPresent
...
```

The Runtime Class spacification uses a `scheuduling: nodeSelector` label to speicify which nodes in a cluster have
the kontain runtime installed.

Currently the DeamonSet tries to install on all Nodes in a cluster.
