# Kontain Integration Into Kubernetes

* See [Quick Start Guide](quickstart.md) for a fast demonstration of Kontain integrated with Kubernetes.

Kubernetes has a mechanism called Runtime Classes that allows new container types to be introduced. Kontain uses
a Kubernetes Runtime Class to introduce itself into the kubernetes ecology. Once introduced, users specify Kontain
as the runtime environment for their container(s) by adding `runtimeClassName: kontain` to their YAML Pod specifications.

Kontain requires that the Kubernetes host nodes run an OCI compliant container manager like CRI-O or `containerd`.
When installed, Kontain modifies the host container manager to recognize start requests for the meant for the Kontain
Runtime Class. The container manager gives those requests to the Kontain supplied KRUN program which is resonsible for 
the actual container start-up..

Kontain is installed on Kubernetes clusters by means of a Daiemonset called `kontain-deploy` in the `kube-system`
namespace. It performs the follwoing tasks:

1. Install Executables

- `/opt/kontain/bin/krun` OCI compliant container runner that knows about the Kontain VM Monitor
- `/opt/kontain/bin/km` Kontain VM Monitor. Provides execution environment.

2. Configure Container Manager

- This is container manager specific
- CRI-O complete, containerd planned


