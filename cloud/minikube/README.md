# Minikube

Minikube runs a single node Kubernetes cluster inside a single docker container running on your local linux system. Once minikube is running all of the standard Kubernetes tools to administer the cluster are available from your local system.

Minikube also supports Mac and Windows by running Linux inside a virtual machine. The minikube container runs inside the virtual machine. This means even our CEO could run a Kubernetes cluster on his laptop.

See <https://minikube.sigs.k8s.io/docs/start/> for installation instructions. (On Fedora, use the RPM variation).

Once running, you can see the minikube Docker container on you local system

```sh
$ docker ps
CONTAINER ID        IMAGE                               ... NAMES
65472993394e        gcr.io/k8s-minikube/kicbase:v0.0.14 ... minikube

```

A nested version of Docker runs inside the minikube docker container, and that docker serves as the image store for the cluster.

Since minikube is just a docker container, you can log into it and look at it's nested docker.

```sh
$ docker exec -it minikube /bin/bash
root@minikube:/# docker ps
CONTAINER ID        IMAGE                  COMMAND                 ...
772282dcc62a        bfe3a36ebd25           "/coredns -conf /etc…"  ...
7a7fefd63e51        bad58561c4be           "/storage-provisioner"  ...
f70676de5023        k8s.gcr.io/pause:3.2   "/pause"                ...
19d5270153b5        k8s.gcr.io/pause:3.2   "/pause"                ...
964800fd1453        635b36f4d89f           "/usr/local/bin/kube…"  ...
84cf52a54e18        k8s.gcr.io/pause:3.2   "/pause"                ...
73cd3b3e4e97        b15c6247777d           "kube-apiserver --ad…"  ...
336a2cffd9fa        14cd22f7abe7           "kube-scheduler --au…"  ...
ae2b0b514766        4830ab618586           "kube-controller-man…"  ...
cb1e11edf58b        0369cf4303ff           "etcd --advertise-cl…"  ...
ebf4a1f31ae0        k8s.gcr.io/pause:3.2   "/pause"                ...
f6d0adbab620        k8s.gcr.io/pause:3.2   "/pause"                ...
007377b8572e        k8s.gcr.io/pause:3.2   "/pause"                ...
680b8127c7d2        k8s.gcr.io/pause:3.2   "/pause"                ...
root@minikube:/# docker images
REPOSITORY                                TAG                 IMAGE ID            CREATED             SIZE
k8s.gcr.io/kube-proxy                     v1.19.4             635b36f4d89f        5 weeks ago         118MB
k8s.gcr.io/kube-apiserver                 v1.19.4             b15c6247777d        5 weeks ago         119MB
k8s.gcr.io/kube-controller-manager        v1.19.4             4830ab618586        5 weeks ago         111MB
k8s.gcr.io/kube-scheduler                 v1.19.4             14cd22f7abe7        5 weeks ago         45.7MB
gcr.io/k8s-minikube/storage-provisioner   v3                  bad58561c4be        3 months ago        29.7MB
k8s.gcr.io/etcd                           3.4.13-0            0369cf4303ff        3 months ago        253MB
kubernetesui/dashboard                    v2.0.3              503bc4b7440b        6 months ago        225MB
k8s.gcr.io/coredns                        1.7.0               bfe3a36ebd25        6 months ago        45.2MB
kubernetesui/metrics-scraper              v1.0.4              86262685d9ab        8 months ago        36.9MB
k8s.gcr.io/pause                          3.2                 80d28bedfe5d        10 months ago       683kB

```

Minikube includes a number of ways to make Docker images created outside the minikube environment available inside minikube. The full set of options can be found at <https://minikube.sigs.k8s.io/docs/handbook/pushing/>. The most straightforward is the `minikube cache` command.

```sh
$ minikube cache add hello-world:latest
$ docker exec -it minikube /bin/bash
root@minikube:/# docker images
REPOSITORY                                TAG                 IMAGE ID            CREATED             SIZE
...
hello-world                               latest              bf756fb1ae65        11 months ago       13.3kB
root@minikube:/# 

```

Note: it is important that the image name includes the label. For example `minikube cache add hello-world` will silently fail.
The container can now be used inside the minikube cluster. If the container is updated on your local system, then `minikube cache reload` must be called. For example:

```sh
$ minikube cache reload hello-world:latest
$
```

## Prerequisities

* kubectl
  * <https://kubernetes.io/docs/tasks/tools/install-kubectl/#install-kubectl-on-linux>
* minikube
  * <https://minikube.sigs.k8s.io/docs/>
* registry  - create and configure
  * info TODO

## Steps automated by scripts here

TODO

## Deploy to Kubernetes cluster

TODO
`make CLOUD=minikube ...`

Assuming you are in the top of KM repo

```bash
kubectl config use-context minikube
kubectl apply -f cloud/k8s/kvm-ds.yml
kubectl apply -k payloads/cloud/minikube

```
