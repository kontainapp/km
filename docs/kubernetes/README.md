# Kontain Integration Into Kubernetes

* See [Quick Start Guide](quickstart.md) for a demonstration of Kontain integrated with Kubernetes.
* See [Architecture Guide](architecture.md) for the Kubertetes Integration Architecture
* See [Installation Guide](installation.md) for Kubernetes Host Installation Instructions

Kontain levereges the Runtime Class abstraction to allow containers controlled by the Kontain Monitor to exist
in Kubernetes clusters. Users define containers controlled by the Kontain Monitor by adding `runtimeClassName: kontain`
to YAML container specifications.

## Old Kubernetes Writeup - To Edit for New Design

Kubernetes needs to be configured to run Kontain workloads in a Kontain VM.

Kontain provides DaemonSet _kontaind_, which automates installation of Kontain Monitor (KM) onto each Kubernetes node where a kontainer will be deployed (i.e., into `/opt/kontain/bin` on each node).

In addition, a kernel virtualization device (KVM or KKM) must be present on each node where a Kontain workload is scheduled. Kontain uses a [third-party KVM device plug-in](https://github.com/kubevirt/kubernetes-device-plugins/blob/master/docs/README.kvm.md) to provide unprivileged pods access to `/dev/kvm`. This plug-in is automatically installed when kontaind is deployed on your Kubernetes cluster.

#### Installing the kontaind DaemonSet

To deploy the latest version of `kontaind`, run:

```
kubectl apply \
  -f https://github.com/kontainapp/km-releases/blob/master/k8s/kontaind/deployment.yaml?raw=true
```

To verify the installation, type:

```
kubectl get daemonsets
```

The number of DESIRED and READY `kontaind` DaemonSets should match, as illustrated below.:

```
NAME               DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE   NODE SELECTOR   AGE
kontaind           1         1         1       1            1           <none>          84d
```
#### Invoking Kontain from Kubernetes

*Known Limitation:* Currently, a manual process is needed to instruct Kubernetes to invoke Kontain Monitor (KM) for your application. We are working to automate the process of invoking  `krun` (Kontain’s OCI runtime) from Kubernetes.

1. Configure the YAML file for your application to explicitly mount Kontain Monitor from the host node(s) into your pod and to make KM available to the containers in the pod. Provide the pod with the mount path to the Kontain release (using `hostPath` volume) and add a command to invoke KM from the container, as shown in the example below.

**EXAMPLE: YAML Config to Invoke KM**

```yaml
apiVersion: v1
kind: Pod
metadata:
 name: dweb
spec:
 containers:
   - name: dweb
     image: kontainkubecr.azurecr.io/demo-runenv-dweb:latest
     imagePullPolicy: Always
     securityContext:
       privileged: false
       allowPrivilegeEscalation: false
     resources:
       requests:
         devices.kubevirt.io/kvm: "1"
         cpu: "250m"
       limits:
         devices.kubevirt.io/kvm: "1"
         cpu: "1"
     command: ["/opt/kontain/bin/km", "/dweb/dweb.km", "8080"]
     volumeMounts:
       - name: kontain-monitor
         mountPath: /opt/kontain
 volumes:
   - name: kontain-monitor
     hostPath:
       path: /opt/kontain
 restartPolicy: Never
```

2. Run your application using the modified YAML:

```
kubectl apply -f file.yaml
```

