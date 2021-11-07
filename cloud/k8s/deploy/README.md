# Kontain Runtime for Kubernetes

This directory tools to install the Kontain runtime on Kubernets clusters. The of installation are
per-node DeamonSet objects. There are two DaemonSets defined here:

- `kkm-install.yaml` - Install the KKM driver
- `k8s-deploy.yaml` - Install the Kontain runtime

## Installing KKM

KKM is a software-only virutalization driver that the Kontain runtime can use when hardware virtualization (KVM)
is not availale or is undesirable. KKM is not a full, general purpose virtualization driver. KKM only supports
the facilities needed by the Kontain runtime. 

To install KKM on all nodes in kubernetes cluster, run:

```
$ kubectl apply -f cm-kkm-install.yaml
$ kubektl apply -f km-install.yaml
```

## Installing Kontain Runtime

* THIS IS A WORK IN PROGRESS *

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

TODO:

The current runtime installation script requires privileges to query the k8s cluster configuration to decide whether to
add itself to the `containerd` or `crio` configuration. This is actually counter to what GKE seems to encourge, pods
having little or no premission to use k8s control plane services. Ideally, it appears that a model where these decisions
are made outside the cluster, say in a script that can run kubectl, fits better.

The KKM installation procedure was inspired by a Google Cloud 'solutions' example of using a DaemonSet. The interesting
ideas introducted here are two:

-  Do the work in an 'init' container that goes away when the work is complete. The long term DaemonSet pod is a simple
`pause` conatainer with no real worker. Seems pretty good from a security POV.

- Use a standard container and do the customization (entrypoint script) as a `config` record.
