# Kontain/Kubernetes Integration - Architecture

## User View

Kontain uses the Kubernetes Container Runtime Class feature
(https://kubernetes.io/docs/concepts/containers/runtime-class/)
to allow the user to select running a container under the control of
the Kontain Virtual Machine Monitor (KM) by using 'runtimeClassName: kontain'
in a containter specification. For example, the following YAML creates a 
pod that runs under the control of the Kontain Runtime. This particular
pod does nothing.:

```
apiVersion: apps/v1
kind: Deployment
metadata:
  name: kontain-test-app
spec:
  selector:
    matchLabels:
      kontain: test-app
  template:
    metadata:
      labels:
        kontain: test-app
    spec:
      runtimeClassName: kontain
      containers:
      - name: kontain-test-app
        image: busybox:latest
        command: [ "sleep", "infinity" ]
        imagePullPolicy: IfNotPresent
```

## Internals

Kontain installs two binaries on every Kubernetes host node.

* KRUN - An OCI compliant runtime that manages containers of Runtime Class 'kontain'.
* KM - The Kontain Virtual Machine monitor that provides the execution environment for processes
running in these containers.

Kontain binaries are installed in the directory `/opt/kointain/bin`.

Kontain integrates with the CRI-O and `containerd` runtime daemons by adding configuration files
that let the daemon know about KRUN. All containers run under the control of the 'kontain' Runtime Class
are started with KRUN.

## KRUN

KRUN is an OCI runtime derived from `crun` that knows about the Kontain Monitor (KM) and Kontain
Virtual Machines.

## KM

KM is the Kontain Virtual Machine Monitor. KM provides the execution exvironment for all processes
run inside a container controllered by the 'kontain' Runtime Class.
