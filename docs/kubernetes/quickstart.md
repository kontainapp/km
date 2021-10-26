# Quickstart Guide for Kubernetes Runtime

This guide shows how to install the Kontain Runtime Class on an Kubernetes cluster and
how to run containers under the Kontain runtime class.

## Step 1 - Start Minikube

```
$ minikube start --container-runtime=cri-o
```

Note: You'll need minikube version v1.22.0 (https://github.com/kubernetes/minikube/releases/tag/v1.22.0) or better.

Check for success with `kubectl get pods -A`:

```
$ kubectl get pods -A
NAMESPACE     NAME                               READY   STATUS    RESTARTS   AGE
kube-system   coredns-558bd4d5db-2mnlw           1/1     Running   0          55s
kube-system   etcd-minikube                      1/1     Running   0          53s
kube-system   kindnet-bw4b5                      1/1     Running   0          55s
kube-system   kube-apiserver-minikube            1/1     Running   0          53s
kube-system   kube-controller-manager-minikube   1/1     Running   0          53s
kube-system   kube-proxy-lpx8t                   1/1     Running   0          55s
kube-system   kube-scheduler-minikube            1/1     Running   0          53s
kube-system   storage-provisioner                1/1     Running   0          67s
```

## Step 2 - Deploy Kontain Runtime

```
kubectl apply -f https://raw.githubusercontent.com/kontainapp/km/latest/cloud/k8s/deploy/k8s-deploy.yaml
```

To verify the installation, type:

```
$ kubectl get daemonsets.apps -A 
NAMESPACE     NAME             DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE   NODE SELECTOR            AGE
kube-system   kindnet          1         1         1       1            1           <none>                   168m
kube-system   kontain-deploy   1         1         1       1            1           <none>                   163m
kube-system   kube-proxy       1         1         1       1            1           kubernetes.io/os=linux   168m
```

A new pod, `kontain-deploy` should appear.

```
$ kubectl get pods -A
NAMESPACE     NAME                               READY   STATUS    RESTARTS   AGE
kube-system   coredns-558bd4d5db-2mnlw           1/1     Running   0          114s
kube-system   etcd-minikube                      1/1     Running   0          112s
kube-system   kindnet-bw4b5                      1/1     Running   0          114s
kube-system   kontain-deploy-qmtct               1/1     Running   0          17s
kube-system   kube-apiserver-minikube            1/1     Running   0          112s
kube-system   kube-controller-manager-minikube   1/1     Running   0          112s
kube-system   kube-proxy-lpx8t                   1/1     Running   0          114s
kube-system   kube-scheduler-minikube            1/1     Running   0          112s
kube-system   storage-provisioner                1/1     Running   0          2m6s
```

## Step 3 - Start Test Kontain Container

```
kubectl apply -f https://raw.githubusercontent.com/kontainapp/km/latest/demo/k8s/test.yaml
```

A new pod, `kontain-test-app-xxxxx` should appear.

```
$ kubectl get pods -A
NAMESPACE     NAME                                READY   STATUS    RESTARTS   AGE
default       kontain-test-app-647874765d-7ftrp   1/1     Running   0          23m
kube-system   coredns-558bd4d5db-2mnlw            1/1     Running   0          36m
kube-system   etcd-minikube                       1/1     Running   0          36m
kube-system   kindnet-bw4b5                       1/1     Running   0          36m
kube-system   kontain-deploy-qmtct                1/1     Running   0          35m
kube-system   kube-apiserver-minikube             1/1     Running   0          36m
kube-system   kube-controller-manager-minikube    1/1     Running   0          36m
kube-system   kube-proxy-lpx8t                    1/1     Running   0          36m
kube-system   kube-scheduler-minikube             1/1     Running   0          36m
kube-system   storage-provisioner                 1/1     Running   0          36m
```

Check that `kontain-test-app` pod runs with Kontain runtime (note to please replace the kontain-test-app-xxxxx with the appropriate pod id).

```
$ kubectl exec -it kontain-test-app-647874765d-7ftrp  -- uname -r
5.13.7-100.fc33.x86_64.kontain.KVM

TIP: to load images built in the local docker registry to minikube, you can use:
$ docker save <image-name> | pv | (eval $(minikube podman-env) && podman load)
```
