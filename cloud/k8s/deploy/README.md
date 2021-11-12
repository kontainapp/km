# Kontain Integration into Kubernetes

The Kontain Runtime is installed into Kubernetes by introducing a new `RuntimeClass` object called `kontain`.
This new `RuntimeClass` is used in Pod specifications as follows:

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

In order to implement this, Kontain needs to be installed on the Kubernetes worker nodes. The installation
involves:

- Add Kontain binaries to worker node.
- Modify worker node container runtime manager (containerd or cri-o) to recognize new `RuntimeClass`.
- (optional) Install KKM virtualization driver.

## Manual Installation Procedure

- `kubectl apply -f runtime-class.yaml`
- `kubectl apply -f cm-install-lib-class.yaml`
- `kubectl apply -f cm-containerd-install.yaml` or `kubectl apply -f cm-crio-install.yaml`
- `kubectl apply -f kontain-deploy.yaml`

The plan is to wrap this up inside a single `kontain-install.sh` that is run from the outside (where
security scope is hopefully not a problem).
