# Kontain Integration Into Kubernetes

* See [Quick Start Guide](quickstart.md) for a fast demonstration of Kontain integrated with Kubernetes.

Kubernetes has a mechanism called Runtime Classes that allows new container types to be introduced. Kontain uses
a Kubernetes Runtime Class to introduce itself into the kubernetes ecology. Once introduced, users specify Kontain
as the runtime environment for their container(s) by adding `runtimeClassName: kontain` to their YAML Pod specifications.

Kontain requires that the Kubernetes host nodes run an OCI compliant container manager like CRI-O or `containerd`.
When Kontain is installed, host container manager's configuration is modified to recognize container start requests targeted
to the Kontain Runtime Class. The container manager routes those requests to the Kontain supplied KRUN program which is responsible for 
the actual container start-up.

Kontain is installed on Kubernetes clusters by a DaemonSet called `kontain-deploy` (in the `kube-system`
namespace). The `kontain-deploy` DaemonSet  performs the following tasks:

1. Install Executables

- `/opt/kontain/bin/krun` OCI compliant container runtime that knows about the Kontain VM Monitor
- `/opt/kontain/bin/km` Kontain VM Monitor. Provides execution environment for processes in the container.
- `/opt/kontain/sshim/containerd-shim-krun-v2` - CRI Runtime Shim for `containerd`.

2. Configure Container Manager

Container manager specific.

- CRI-O complete. 
- Containerd complete.


