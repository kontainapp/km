# Kontain/Kubernetes Integration - Architecture

Kontain uses the Kubernetes Container Runtime Class feature
(https://kubernetes.io/docs/concepts/containers/runtime-class/)
to allow the user to select running a container under the control of
the Kontain Virtual Machine Monitor (KM) by using 'runtimeClassName: kontain'
in a containter specification. For example:

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

Kontain installs two binaries on every Kubernetes host.

* KRUN - An OCI compliant runtime that manages containers of Runtime Class 'kontain'.
* KM - The Kontain Virtual Machine monitor that provides the execution environment for processes
running in these containers.

TODO:

Describe KRUN and KM.
Describe how KRUN is integrated with various contain runtime options (CRI-O, etc?).
Describe how Kubernetes 'kontain' Runtime Class.
